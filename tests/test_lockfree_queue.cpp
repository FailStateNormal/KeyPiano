// Unit + stress tests for the lock-free audio queues.
//
// Coverage:
//   * SPSC  — single-thread FIFO order, full/empty boundaries, and a
//             two-thread producer/consumer stress run that checks every value
//             is received exactly once and in order.
//   * MPSC  — single-thread FIFO order, full/empty boundaries, and a
//             many-producer / one-consumer stress run that checks no item is
//             lost or duplicated across producers.

#include "audio/LockFreeQueue.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

using keypiano::audio::MpscQueue;
using keypiano::audio::SpscQueue;

namespace {

// A small trivially-copyable payload, exercising a non-scalar element type.
struct Item {
  std::uint32_t producer;
  std::uint32_t value;
};

}  // namespace

// ---------------------------------------------------------------------------
// SPSC
// ---------------------------------------------------------------------------

TEST(SpscQueue, FifoOrderSingleThread) {
  SpscQueue<int, 8> q;
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(q.push(i));
  }
  for (int i = 0; i < 5; ++i) {
    int out = -1;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, i);
  }
}

TEST(SpscQueue, PopOnEmptyReturnsFalse) {
  SpscQueue<int, 4> q;
  int out = 123;
  EXPECT_FALSE(q.pop(out));
  EXPECT_EQ(out, 123);  // left untouched
}

TEST(SpscQueue, PushOnFullReturnsFalse) {
  SpscQueue<int, 4> q;  // can hold exactly 4 (no wasted slot)
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_TRUE(q.push(3));
  EXPECT_TRUE(q.push(4));
  EXPECT_FALSE(q.push(5));  // full
  int out = 0;
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 1);
  EXPECT_TRUE(q.push(5));  // room again
}

TEST(SpscQueue, WrapAroundReusesSlots) {
  SpscQueue<int, 4> q;
  // Push/pop many more than Capacity to force the ring index to wrap.
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q.push(i));
    int out = -1;
    ASSERT_TRUE(q.pop(out));
    ASSERT_EQ(out, i);
    ASSERT_TRUE(q.empty());
  }
}

TEST(SpscQueue, TwoThreadStress) {
  constexpr int kCount = 1'000'000;
  SpscQueue<std::uint32_t, 1024> q;

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!q.push(static_cast<std::uint32_t>(i))) {
        std::this_thread::yield();
      }
    }
  });

  // Consumer must see a strictly increasing sequence 0..kCount-1.
  std::uint32_t expected = 0;
  std::uint32_t received = 0;
  while (received < kCount) {
    std::uint32_t out = 0;
    if (q.pop(out)) {
      ASSERT_EQ(out, expected);
      ++expected;
      ++received;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();
  EXPECT_EQ(received, static_cast<std::uint32_t>(kCount));
}

// ---------------------------------------------------------------------------
// MPSC
// ---------------------------------------------------------------------------

TEST(MpscQueue, FifoOrderSingleProducer) {
  MpscQueue<int, 8> q;
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(q.push(i));
  }
  for (int i = 0; i < 5; ++i) {
    int out = -1;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out, i);
  }
}

TEST(MpscQueue, PopOnEmptyReturnsFalse) {
  MpscQueue<int, 4> q;
  int out = 99;
  EXPECT_FALSE(q.pop(out));
}

TEST(MpscQueue, PushOnFullReturnsFalse) {
  MpscQueue<int, 4> q;
  EXPECT_TRUE(q.push(1));
  EXPECT_TRUE(q.push(2));
  EXPECT_TRUE(q.push(3));
  EXPECT_TRUE(q.push(4));
  EXPECT_FALSE(q.push(5));  // full
  int out = 0;
  EXPECT_TRUE(q.pop(out));
  EXPECT_EQ(out, 1);
  EXPECT_TRUE(q.push(5));
}

TEST(MpscQueue, WrapAroundReusesSlots) {
  MpscQueue<int, 4> q;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q.push(i));
    int out = -1;
    ASSERT_TRUE(q.pop(out));
    ASSERT_EQ(out, i);
  }
}

TEST(MpscQueue, ManyProducersStress) {
  constexpr std::uint32_t kProducers = 4;
  constexpr std::uint32_t kPerProducer = 250'000;
  constexpr std::uint32_t kTotal = kProducers * kPerProducer;
  MpscQueue<Item, 1024> q;

  std::vector<std::thread> producers;
  for (std::uint32_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (std::uint32_t v = 0; v < kPerProducer; ++v) {
        while (!q.push(Item{p, v})) {
          std::this_thread::yield();
        }
      }
    });
  }

  // Per producer, values must arrive in the order they were pushed (each
  // producer is itself single-threaded), and the grand total must be exact
  // with no duplicates.
  std::vector<std::uint32_t> next_expected(kProducers, 0);
  std::uint32_t received = 0;
  while (received < kTotal) {
    Item out{};
    if (q.pop(out)) {
      ASSERT_LT(out.producer, kProducers);
      EXPECT_EQ(out.value, next_expected[out.producer])
          << "producer " << out.producer << " out of order";
      ++next_expected[out.producer];
      ++received;
    } else {
      std::this_thread::yield();
    }
  }
  for (auto& t : producers) {
    t.join();
  }

  EXPECT_EQ(received, kTotal);
  for (std::uint32_t p = 0; p < kProducers; ++p) {
    EXPECT_EQ(next_expected[p], kPerProducer);
  }
}
