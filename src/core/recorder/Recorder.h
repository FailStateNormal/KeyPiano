#ifndef KEYPIANO_CORE_RECORDER_RECORDER_H_
#define KEYPIANO_CORE_RECORDER_RECORDER_H_

#include "recorder/KpsFormat.h"
#include "audio/MidiEvent.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Forward declaration avoids pulling in RtAudio headers from Recorder.h.
namespace keypiano::audio { class AudioEngine; }

namespace keypiano {

// Records and plays back MIDI event sequences.
//
// Thread model:
//   onMidiEvent() — audio thread only; zero malloc, zero lock-wait.
//   start/stop*() — UI/main thread only.
//   playbackThread() — spawned internally; joins on stopPlayback() or dtor.
//
// State machine:
//   Idle ──startRecording()──► Recording ──stopRecording()──► Idle
//   Idle ──startPlayback()───► Playing   ─(done/stopped)───► Idle
//
// The internal event buffer is pre-sized to max_events in the constructor and
// never reallocated. onMidiEvent uses an index counter (not push_back) so the
// audio callback never triggers heap allocation.
class Recorder {
 public:
  enum class State : uint8_t { Idle, Recording, Playing };

  // Callback invoked by the playback thread for each event.
  // In production: Recorder::makeAudioDispatch(engine).
  // In tests: a lambda that captures received events.
  using DispatchFn = std::function<void(const MidiEvent&)>;

  explicit Recorder(DispatchFn dispatch, size_t max_events = 65535);
  ~Recorder();

  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  // ── Audio-thread API ────────────────────────────────────────────────────────
  void onMidiEvent(const MidiEvent& event);

  // ── Control API (UI/main thread) ────────────────────────────────────────────
  bool startRecording();  // Idle → Recording; resets buffer. False if not Idle.
  bool stopRecording();   // Recording → Idle.  False if not Recording.
  bool startPlayback();   // Idle → Playing.    False if not Idle or no events.
  void stopPlayback();    // Playing → Idle.    No-op if not Playing.

  State  state()      const { return state_.load(std::memory_order_acquire); }
  size_t eventCount() const { return event_count_.load(std::memory_order_acquire); }

  // ── Persistence (call only in Idle state) ───────────────────────────────────
  bool saveToFile(const std::string& path, const KpsMeta& meta,
                  std::string* err = nullptr) const;

  bool loadFromFile(const std::string& path, KpsMeta* meta,
                    std::string* err = nullptr);

  // Builds a DispatchFn that routes events to the given AudioEngine.
  static DispatchFn makeAudioDispatch(audio::AudioEngine* engine);

 private:
  void playbackThread();

  DispatchFn             dispatch_;
  size_t                 max_events_;
  std::vector<MidiEvent> events_;        // pre-sized; never reallocated
  std::atomic<size_t>    event_count_{0};
  std::atomic<State>     state_{State::Idle};
  std::thread            playback_thread_;
  std::atomic<bool>      stop_flag_{false};
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_RECORDER_RECORDER_H_
