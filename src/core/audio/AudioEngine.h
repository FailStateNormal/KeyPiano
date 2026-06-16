// keypiano — audio engine: owns the WASAPI stream and the cross-thread queues.
//
// AudioEngine is the hub of the three-thread model (plan/PLAN_DETAIL.txt §3):
//
//   Hook thread + UI thread  --post*()-->  event_queue (MPSC)  --+
//                                                                 |
//                                              audio thread (audioCallback)
//                                                                 |
//   UI thread  <--popFeedback()--  feedback_queue (SPSC)  <-------+
//
// The post*() methods are safe to call from any thread; they only push a POD
// MidiEvent onto the lock-free event_queue. The audio thread drains that queue,
// drives the synth, renders one buffer, and publishes note activity back onto
// the feedback_queue for the UI to poll.
//
// AudioEngine does NOT own the synth — the caller creates it (e.g. via
// createFluidSynth()) and keeps it alive for the engine's lifetime. open()
// initialises the synth to the negotiated stream format; close() shuts it down.
// Instruments are loaded by the caller via SynthesizerBase::loadInstrument()
// after open() returns (FluidSynth's load is safe to call on a live stream).

#ifndef KEYPIANO_CORE_AUDIO_AUDIOENGINE_H_
#define KEYPIANO_CORE_AUDIO_AUDIOENGINE_H_

#include <rtaudio/RtAudio.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "AudioCallback.h"
#include "LockFreeQueue.h"
#include "MidiEvent.h"
#include "../synth/SynthesizerBase.h"

namespace keypiano::audio {

// Queue capacities are fixed by the design (plan/PLAN_DETAIL.txt §3):
//   event_queue    — Hook + UI  →  Audio, MPSC, 1024 slots
//   feedback_queue — Audio      →  UI,    SPSC, 256 slots
inline constexpr std::size_t kEventQueueCapacity = 1024;
inline constexpr std::size_t kFeedbackQueueCapacity = 256;

// Output-stage master gain at the volume slider's full scale (100%). >1 amplifies
// the mixed buffer; the default SF2 renders quiet with plenty of headroom, so 3x
// is a usable "loud but clean" ceiling. The UI maps 0..100% linearly onto 0..this.
inline constexpr float kMaxMasterGain = 3.0f;

using EventQueue = MpscQueue<MidiEvent, kEventQueueCapacity>;
using FeedbackQueue = SpscQueue<MidiEvent, kFeedbackQueueCapacity>;

// Runtime metrics, read by the UI thread. buffer_underruns / cpu_load /
// latency_us are written by the audio thread; event_drops is written by the
// producer threads (hook/UI) and feedback_drops by the audio thread. All updates
// are relaxed atomics — the values are diagnostics, not synchronisation.
struct Stats {
  std::atomic<uint32_t> buffer_underruns{0};  // cumulative WASAPI underruns
  std::atomic<double>   cpu_load{0.0};         // last callback time / budget
  std::atomic<uint32_t> latency_us{0};         // one-buffer latency estimate
  // Input MIDI events dropped because the event queue was full (the audio thread
  // is overrunning badly) — these are lost/late notes. Cumulative since open().
  std::atomic<uint32_t> event_drops{0};
  // UI note-activity events dropped because the feedback queue was full (the UI
  // is behind) — cosmetic only (a key highlight may be missed). Cumulative.
  std::atomic<uint32_t> feedback_drops{0};
};

// Everything the audio thread needs, gathered behind one stable pointer so the
// callback only chases POD pointers (no Qt, no STL container ops, no locks).
struct CallbackContext {
  SynthesizerBase* synth = nullptr;
  EventQueue*      event_queue = nullptr;
  FeedbackQueue*   feedback_queue = nullptr;
  Stats*           stats = nullptr;
  uint32_t         sample_rate = 44100;
  // Output-stage master volume multiplier (>1 amplifies). The audio thread reads
  // it (relaxed) and scales the mixed buffer by it after render(). Non-owning
  // pointer into AudioEngine::master_gain_; null means "leave the output
  // unscaled" (unity).
  const std::atomic<float>* master_gain = nullptr;
};

class AudioEngine {
 public:
  struct Config {
    uint32_t    sample_rate = 44100;
    uint32_t    buffer_frames = 256;  // ~5.8 ms @ 44100 Hz
    std::string device_name;          // empty = system default output
  };

  AudioEngine();
  ~AudioEngine();

  // Owns the RtAudio handle and the queues — neither copyable nor movable.
  // (The audio thread holds a pointer to context_, which embeds these.)
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  // Opens + starts the WASAPI stream and initialises `synth` to the negotiated
  // format. `synth` must outlive the engine (non-owning). Returns false on any
  // failure (no device, RtAudio error, synth init failure); the engine is left
  // closed in that case. Call loadInstrument() on the synth afterwards.
  bool open(const Config& cfg, SynthesizerBase* synth);

  // Stops + closes the stream and shuts the synth down. Safe to call when not
  // open. Idempotent.
  void close();

  bool isOpen() const { return open_; }

  // --- Producer API: callable from the hook thread or the UI thread ---------
  // Each pushes one MidiEvent onto event_queue, time-stamped relative to
  // open(). Best-effort: if the queue is full the event is dropped (the audio
  // thread is never blocked or made to wait).
  void postNoteOn(uint8_t ch, uint8_t note, uint8_t vel);
  void postNoteOff(uint8_t ch, uint8_t note);
  void postControlChange(uint8_t ch, uint8_t cc, uint8_t val);
  void postAllNotesOff(uint8_t ch);

  // --- Consumer API: UI thread only -----------------------------------------
  // Pops one note-activity event published by the audio thread. Returns false
  // when no feedback is pending. Drives PianoWidget key highlighting (Phase 3).
  bool popFeedback(MidiEvent& out) { return feedback_queue_.pop(out); }

  const Stats& stats() const { return stats_; }

  // Output-stage master volume, applied by the audio thread after the synth
  // renders. `gain` is clamped to [0, kMaxMasterGain] (1.0 = unchanged, 0.0 =
  // silent, >1 amplifies). Safe to call from any thread; the audio thread reads
  // it with a relaxed atomic.
  // The value survives close()/open() on the same engine, but a freshly
  // constructed engine starts at unity — the UI re-applies it after each start.
  void setMasterGain(float gain);
  float masterGain() const {
    return master_gain_.load(std::memory_order_relaxed);
  }

  // Actual negotiated buffer size after open() (RtAudio may adjust the request).
  uint32_t bufferFrames() const { return config_.buffer_frames; }

 private:
  void post(EventType type, uint8_t ch, uint8_t a, uint8_t b);

  EventQueue      event_queue_;
  FeedbackQueue   feedback_queue_;
  Stats           stats_;
  std::atomic<float> master_gain_{1.0f};  // output-stage volume, see setMasterGain
  CallbackContext context_;
  RtAudio         dac_;
  Config          config_;
  bool            open_ = false;
  std::chrono::steady_clock::time_point start_time_{};
};

}  // namespace keypiano::audio

#endif  // KEYPIANO_CORE_AUDIO_AUDIOENGINE_H_
