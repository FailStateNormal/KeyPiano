// test_audio_callback.cpp — failure-path tests for the audio thread's callback
// that need no audio device. audioCallback() is a free function operating on a
// CallbackContext, so a test can drive it directly with its own queues + a stub
// synth and assert the feedback-drop accounting (P7-3 ⑥: the feedback_drops
// counter, which needs a CallbackContext to exercise).

#include "audio/AudioCallback.h"
#include "audio/AudioEngine.h"  // CallbackContext, queues, Stats, capacities
#include "synth/SynthesizerBase.h"

#include <atomic>
#include <vector>

#include <gtest/gtest.h>

using namespace keypiano;
using namespace keypiano::audio;

namespace {

// Minimal in-memory synth: counts the audio-thread calls, renders nothing. Lets
// the callback run with no real backend / device.
class StubSynth : public SynthesizerBase {
 public:
  bool init(uint32_t, uint32_t) override { return true; }
  void shutdown() override {}
  bool loadInstrument(const std::string&) override { return true; }
  void noteOn(uint8_t, uint8_t, uint8_t) override { ++note_ons; }
  void noteOff(uint8_t, uint8_t) override { ++note_offs; }
  void controlChange(uint8_t, uint8_t, uint8_t) override { ++control_changes; }
  void allNotesOff() override { ++all_notes_off; }
  void render(float*, uint32_t) override { ++renders; }

  int note_ons = 0;
  int note_offs = 0;
  int control_changes = 0;
  int all_notes_off = 0;
  int renders = 0;
};

// Wire a context over caller-owned queues/stats/synth.
struct Harness {
  StubSynth       synth;
  EventQueue      eq;
  FeedbackQueue   fq;
  Stats           stats;
  CallbackContext ctx;
  std::vector<float> buf;

  explicit Harness(unsigned frames = 256) : buf(frames * 2, 0.0f) {
    ctx.synth = &synth;
    ctx.event_queue = &eq;
    ctx.feedback_queue = &fq;
    ctx.stats = &stats;
    ctx.sample_rate = 44100;
  }

  void run(unsigned frames = 256) {
    audioCallback(buf.data(), nullptr, frames, 0.0, 0, &ctx);
  }
};

}  // namespace

// When the feedback queue is full, every note event the callback forwards is
// dropped and counted — but the event is still delivered to the synth.
TEST(AudioCallbackStats, CountsFeedbackDropsWhenFeedbackQueueFull) {
  Harness h;

  // Fill the feedback queue to capacity so no further feedback can be pushed.
  while (h.fq.push(MidiEvent{EventType::NoteOn, 0, 60, 100, 0})) {}

  constexpr int kNotes = 10;
  for (int i = 0; i < kNotes; ++i)
    ASSERT_TRUE(h.eq.push(MidiEvent{EventType::NoteOn, 0, 60, 100, 0}));

  h.run();

  EXPECT_EQ(h.stats.feedback_drops.load(std::memory_order_relaxed),
            static_cast<uint32_t>(kNotes));
  EXPECT_EQ(h.synth.note_ons, kNotes);  // delivery to the synth is unaffected
}

// With room in the feedback queue, nothing is dropped and the note activity is
// forwarded for the UI to poll.
TEST(AudioCallbackStats, NoDropsWhenFeedbackQueueHasRoom) {
  Harness h;

  constexpr int kNotes = 5;
  for (int i = 0; i < kNotes; ++i)
    ASSERT_TRUE(h.eq.push(MidiEvent{EventType::NoteOn, 0, 60, 100, 0}));

  h.run();

  EXPECT_EQ(h.stats.feedback_drops.load(std::memory_order_relaxed), 0u);

  int forwarded = 0;
  MidiEvent e;
  while (h.fq.pop(e)) ++forwarded;
  EXPECT_EQ(forwarded, kNotes);
}

// Control-change events drive the synth but are NOT forwarded as feedback (they
// are not note activity), so they never touch the feedback queue or its counter.
TEST(AudioCallbackStats, ControlChangeProducesNoFeedback) {
  Harness h;

  ASSERT_TRUE(h.eq.push(MidiEvent{EventType::ControlChange, 0, 64, 127, 0}));
  h.run();

  EXPECT_EQ(h.synth.control_changes, 1);
  EXPECT_EQ(h.stats.feedback_drops.load(std::memory_order_relaxed), 0u);
  MidiEvent e;
  EXPECT_FALSE(h.fq.pop(e));  // nothing was forwarded
}

// A starts-clean sanity check mirroring the engine-level counter test.
TEST(AudioCallbackStats, StartsAtZeroAndRenders) {
  Harness h;
  h.run();
  EXPECT_EQ(h.stats.feedback_drops.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(h.synth.renders, 1);  // one buffer mixed per callback
}

namespace {

// A synth that writes a constant into every output sample, so a test can observe
// the callback's output-stage master-gain multiply (StubSynth renders nothing).
class ConstSynth : public StubSynth {
 public:
  void render(float* out, uint32_t frames) override {
    for (uint32_t i = 0; i < frames * 2; ++i) out[i] += value;
    ++renders;
  }
  float value = 0.5f;
};

// Run one callback over a ConstSynth(value) with the given master-gain pointer
// and return a representative output sample (all samples are identical here).
float runWithGain(const std::atomic<float>* gain, float value = 0.5f) {
  ConstSynth synth;
  synth.value = value;
  EventQueue eq;
  FeedbackQueue fq;
  Stats stats;
  CallbackContext ctx;
  ctx.synth = &synth;
  ctx.event_queue = &eq;
  ctx.feedback_queue = &fq;
  ctx.stats = &stats;
  ctx.sample_rate = 44100;
  ctx.master_gain = gain;
  std::vector<float> buf(256 * 2, 0.0f);
  audioCallback(buf.data(), nullptr, 256, 0.0, 0, &ctx);
  return buf[256];  // a mid-buffer sample; they're all equal
}

}  // namespace

// Master gain scales the rendered buffer: synth writes 0.5, gain 0.5 -> 0.25.
TEST(AudioCallbackGain, ScalesOutputByGain) {
  std::atomic<float> gain{0.5f};
  EXPECT_FLOAT_EQ(runWithGain(&gain), 0.25f);
}

// Unity gain leaves the output untouched (the callback skips the multiply).
TEST(AudioCallbackGain, UnityGainLeavesOutputUnchanged) {
  std::atomic<float> gain{1.0f};
  EXPECT_FLOAT_EQ(runWithGain(&gain), 0.5f);
}

// A null master_gain pointer means "unscaled" — same result as unity.
TEST(AudioCallbackGain, NullPointerLeavesOutputUnchanged) {
  EXPECT_FLOAT_EQ(runWithGain(nullptr), 0.5f);
}

// Zero gain silences the output.
TEST(AudioCallbackGain, ZeroGainSilences) {
  std::atomic<float> gain{0.0f};
  EXPECT_FLOAT_EQ(runWithGain(&gain), 0.0f);
}
