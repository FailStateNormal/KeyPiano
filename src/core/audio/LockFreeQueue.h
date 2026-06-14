// keypiano — lock-free queues for the audio data path
//
// Two bounded, header-only queues used to move events across thread
// boundaries without locks (see plan/PLAN_DETAIL.txt §3 "线程模型"):
//
//   MpscQueue  — multi-producer / single-consumer.
//                event_queue: Hook thread + UI thread  →  Audio thread.
//                Producers (hook/UI) are lock-free; the consumer (audio
//                thread) is wait-free: at most one failed read, never a spin.
//
//   SpscQueue  — single-producer / single-consumer.
//                feedback_queue: Audio thread  →  UI thread.
//                Both ends are wait-free.
//
// Audio-thread contract (铁律): the consuming side performs zero allocation,
// zero locking and zero unbounded spinning. All storage is preallocated at
// construction; push/pop only touch the ring buffer and a couple of atomics.
//
// The MPSC queue is Dmitry Vyukov's bounded MPMC algorithm specialised for a
// single consumer (the dequeue side drops its CAS loop since only one thread
// ever dequeues).

#ifndef KEYPIANO_CORE_AUDIO_LOCKFREEQUEUE_H_
#define KEYPIANO_CORE_AUDIO_LOCKFREEQUEUE_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace keypiano::audio {

// We deliberately alignas(kCacheLineSize) the hot atomics and the ring buffer
// to keep producer/consumer state on separate cache lines (false-sharing
// avoidance). MSVC warns C4324 "structure was padded due to alignment
// specifier" for exactly this — which is the intent, not a defect — so silence
// it locally rather than dropping the project-wide /W4 /WX.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

// Cache line size used to pad hot atomics apart and avoid false sharing.
// 64 bytes is correct for every x86-64 target we ship on.
inline constexpr std::size_t kCacheLineSize = 64;

// ---------------------------------------------------------------------------
// SpscQueue: single-producer, single-consumer bounded ring.
//
//   producer side: push()  — one thread only
//   consumer side: pop()   — one (different) thread only
//
// Both operations are wait-free. Positions are monotonically increasing
// 64-bit counters; the difference between them is the live element count, so
// there is no full/empty ambiguity and no need to waste a slot.
// ---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class SpscQueue {
  static_assert(std::has_single_bit(Capacity),
                "SpscQueue Capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>,
                "SpscQueue element type must be trivially copyable "
                "(audio-path types are PODs)");

 public:
  SpscQueue() = default;
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  // Producer side. Returns false if the queue is full (item not stored).
  bool push(const T& item) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    // head_ is published by the consumer with release; acquire it so we see a
    // truthful "how full are we" before deciding to overwrite a slot.
    if (tail - head_.load(std::memory_order_acquire) >= Capacity) {
      return false;  // full
    }
    buffer_[tail & kMask] = item;
    // release: the data store above must be visible before the consumer sees
    // the advanced tail.
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer side. Returns false if the queue is empty (out left untouched).
  bool pop(T& out) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    // acquire the producer's tail so the data write paired with it is visible.
    if (head == tail_.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    out = buffer_[head & kMask];
    // release: frees the slot for the producer to reuse.
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }

  static constexpr std::size_t capacity() { return Capacity; }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};  // consumer-owned
  alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};  // producer-owned
  alignas(kCacheLineSize) T buffer_[Capacity]{};
};

// ---------------------------------------------------------------------------
// MpscQueue: multi-producer, single-consumer bounded ring.
//
//   producer side: push()  — any number of threads, lock-free
//   consumer side: pop()   — exactly one thread, wait-free
//
// Each cell carries a sequence counter. A producer claims the next enqueue
// slot via CAS on enqueue_pos_, writes its data, then publishes the cell by
// bumping the cell sequence. The lone consumer never CASes: it reads its own
// dequeue_pos_, checks the cell is published, takes the data and releases the
// slot. This keeps the audio thread (the consumer) free of spin loops.
// ---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class MpscQueue {
  static_assert(std::has_single_bit(Capacity),
                "MpscQueue Capacity must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>,
                "MpscQueue element type must be trivially copyable "
                "(audio-path types are PODs)");

 public:
  MpscQueue() {
    // Seed each cell's sequence with its index. A cell is "ready to enqueue at
    // position p" exactly when its sequence equals p.
    for (std::size_t i = 0; i < Capacity; ++i) {
      buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }
  MpscQueue(const MpscQueue&) = delete;
  MpscQueue& operator=(const MpscQueue&) = delete;

  // Producer side (multi-thread safe). Returns false if the queue is full.
  bool push(const T& item) {
    Cell* cell;
    std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    for (;;) {
      cell = &buffer_[pos & kMask];
      const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
      const std::intptr_t diff =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
      if (diff == 0) {
        // Cell is free at pos; try to claim it.
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
        // CAS failed: pos was refreshed with the current value, retry.
      } else if (diff < 0) {
        return false;  // full: the cell is still owned by an un-consumed item
      } else {
        // Another producer claimed this pos already; refresh and retry.
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }
    cell->data = item;
    // Publish: release so the consumer's acquire sees the data store.
    cell->sequence.store(pos + 1, std::memory_order_release);
    return true;
  }

  // Consumer side (single thread only). Wait-free. Returns false if empty.
  bool pop(T& out) {
    // dequeue_pos_ is written only by this same single consumer, so relaxed is
    // sufficient for our own bookkeeping.
    const std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    Cell* cell = &buffer_[pos & kMask];
    const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
    const std::intptr_t diff = static_cast<std::intptr_t>(seq) -
                               static_cast<std::intptr_t>(pos + 1);
    if (diff == 0) {
      out = cell->data;
      // Advance our read cursor, then release the slot for reuse one lap
      // later by setting its sequence to pos + Capacity.
      dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
      cell->sequence.store(pos + Capacity, std::memory_order_release);
      return true;
    }
    // diff < 0 means the cell isn't published yet -> empty. (diff > 0 cannot
    // happen for a single consumer.)
    return false;
  }

  static constexpr std::size_t capacity() { return Capacity; }

 private:
  struct Cell {
    std::atomic<std::size_t> sequence;
    T data;
  };

  static constexpr std::size_t kMask = Capacity - 1;

  alignas(kCacheLineSize) Cell buffer_[Capacity];
  alignas(kCacheLineSize) std::atomic<std::size_t> enqueue_pos_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> dequeue_pos_{0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace keypiano::audio

#endif  // KEYPIANO_CORE_AUDIO_LOCKFREEQUEUE_H_
