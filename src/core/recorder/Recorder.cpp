#include "recorder/Recorder.h"
#include "audio/AudioEngine.h"

#include <algorithm>
#include <chrono>

namespace keypiano {

// ── Constructor / destructor ───────────────────────────────────────────────────

Recorder::Recorder(DispatchFn dispatch, size_t max_events)
    : dispatch_(std::move(dispatch)), max_events_(max_events) {
  events_.resize(max_events_);  // pre-size; capacity fixed for the object's lifetime
}

Recorder::~Recorder() {
  // Always signal stop and join — even if playback finished naturally, the
  // thread object remains joinable until we call join() or detach(). Destroying
  // a joinable std::thread calls std::terminate().
  stop_flag_.store(true, std::memory_order_release);
  if (playback_thread_.joinable()) playback_thread_.join();
}

// ── Audio-thread API ──────────────────────────────────────────────────────────
//
// Invariants:
//   • Only the audio thread calls this; no concurrent writes to events_.
//   • event_count_ is the sole write-coordination point: written here (release)
//     and read by UI/playback threads (acquire).
//   • Zero dynamic allocation, zero spin-wait, zero mutex.

void Recorder::onMidiEvent(const MidiEvent& event) {
  if (state_.load(std::memory_order_acquire) != State::Recording) return;
  // Relaxed load is safe: only this thread (audio) ever writes event_count_
  // while Recording. The acquire on state_ above already establishes ordering
  // with startRecording()'s seq_cst store.
  size_t cnt = event_count_.load(std::memory_order_relaxed);
  if (cnt >= max_events_) return;
  events_[cnt] = event;
  event_count_.store(cnt + 1, std::memory_order_release);
}

// ── Control API ───────────────────────────────────────────────────────────────

bool Recorder::startRecording() {
  // Reset counter before making Recording visible so the audio thread always
  // starts from index 0. seq_cst on both ensures total ordering.
  event_count_.store(0, std::memory_order_seq_cst);
  State expected = State::Idle;
  return state_.compare_exchange_strong(expected, State::Recording,
                                        std::memory_order_seq_cst,
                                        std::memory_order_relaxed);
}

bool Recorder::stopRecording() {
  State expected = State::Recording;
  return state_.compare_exchange_strong(expected, State::Idle,
                                        std::memory_order_seq_cst,
                                        std::memory_order_relaxed);
}

bool Recorder::startPlayback() {
  if (event_count_.load(std::memory_order_acquire) == 0) return false;
  State expected = State::Idle;
  if (!state_.compare_exchange_strong(expected, State::Playing,
                                      std::memory_order_seq_cst,
                                      std::memory_order_relaxed)) {
    return false;
  }
  // Join any previously completed playback thread before creating a new one.
  // A std::thread that finished but was never joined is still joinable, and
  // assigning to a joinable thread calls std::terminate().
  if (playback_thread_.joinable()) playback_thread_.join();
  stop_flag_.store(false, std::memory_order_release);
  playback_thread_ = std::thread(&Recorder::playbackThread, this);
  return true;
}

void Recorder::stopPlayback() {
  // Signal stop and always join — even if the thread finished naturally it
  // remains joinable until joined. State is set to Idle by the thread itself.
  stop_flag_.store(true, std::memory_order_release);
  if (playback_thread_.joinable()) playback_thread_.join();
}

// ── Playback thread ───────────────────────────────────────────────────────────

void Recorder::playbackThread() {
  size_t count = event_count_.load(std::memory_order_acquire);
  auto t0 = std::chrono::steady_clock::now();

  for (size_t i = 0; i < count; ++i) {
    if (stop_flag_.load(std::memory_order_acquire)) break;

    auto target = t0 + std::chrono::microseconds(events_[i].ts_us);
    std::this_thread::sleep_until(target);

    if (stop_flag_.load(std::memory_order_acquire)) break;

    dispatch_(events_[i]);
  }

  state_.store(State::Idle, std::memory_order_release);
}

// ── Persistence ───────────────────────────────────────────────────────────────

bool Recorder::saveToFile(const std::string& path, const KpsMeta& meta,
                           std::string* err) const {
  size_t count = event_count_.load(std::memory_order_acquire);
  std::vector<MidiEvent> evs(events_.begin(),
                              events_.begin() + static_cast<ptrdiff_t>(count));
  return KpsFormat::write(path, meta, evs, err);
}

bool Recorder::loadFromFile(const std::string& path, KpsMeta* meta,
                             std::string* err) {
  std::vector<MidiEvent> loaded;
  KpsMeta m;
  if (!KpsFormat::read(path, &m, &loaded, err)) return false;
  if (loaded.size() > max_events_) {
    if (err) *err = "file has " + std::to_string(loaded.size()) +
                    " events, exceeds recorder capacity " +
                    std::to_string(max_events_);
    return false;
  }
  std::copy(loaded.begin(), loaded.end(), events_.begin());
  event_count_.store(loaded.size(), std::memory_order_release);
  if (meta) *meta = std::move(m);
  return true;
}

// ── Factory helper ────────────────────────────────────────────────────────────

Recorder::DispatchFn Recorder::makeAudioDispatch(audio::AudioEngine* engine) {
  return [engine](const MidiEvent& e) {
    switch (e.type) {
      case EventType::NoteOn:
        engine->postNoteOn(e.chan, e.note, e.vel);
        break;
      case EventType::NoteOff:
        engine->postNoteOff(e.chan, e.note);
        break;
      case EventType::ControlChange:
        engine->postControlChange(e.chan, e.note, e.vel);
        break;
      case EventType::AllNotesOff:
        engine->postAllNotesOff(e.chan);
        break;
    }
  };
}

}  // namespace keypiano
