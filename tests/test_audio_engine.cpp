// test_audio_engine.cpp — failure-path tests for AudioEngine that need no audio
// device. The engine is never open()ed, so no audio thread drains the event
// queue: posts beyond its capacity are dropped, exercising the drop-counting
// added for the "queue drop statistics" work (P7-3 ③).

#include "audio/AudioEngine.h"

#include <gtest/gtest.h>

using namespace keypiano;

// Posting exactly the queue capacity with nothing draining must not drop; every
// post beyond that is dropped and counted in stats().event_drops.
TEST(AudioEngineStats, CountsDroppedEventsWhenQueueFull) {
  audio::AudioEngine engine;  // not opened — no callback consumes the queue

  for (std::size_t i = 0; i < audio::kEventQueueCapacity; ++i)
    engine.postNoteOn(0, 60, 100);
  EXPECT_EQ(engine.stats().event_drops.load(std::memory_order_relaxed), 0u);

  constexpr uint32_t kExtra = 50;
  for (uint32_t i = 0; i < kExtra; ++i)
    engine.postNoteOn(0, 60, 100);
  EXPECT_EQ(engine.stats().event_drops.load(std::memory_order_relaxed), kExtra);
}

// All post*() variants funnel through the same drop accounting.
TEST(AudioEngineStats, AllPostKindsCountDrops) {
  audio::AudioEngine engine;

  // Fill the queue first.
  for (std::size_t i = 0; i < audio::kEventQueueCapacity; ++i)
    engine.postNoteOn(0, 60, 100);
  ASSERT_EQ(engine.stats().event_drops.load(std::memory_order_relaxed), 0u);

  // Each of these now finds the queue full and must be counted as a drop.
  engine.postNoteOn(0, 60, 100);
  engine.postNoteOff(0, 60);
  engine.postControlChange(0, 64, 127);
  engine.postAllNotesOff(0);
  EXPECT_EQ(engine.stats().event_drops.load(std::memory_order_relaxed), 4u);
}

// A fresh engine starts with clean counters.
TEST(AudioEngineStats, StartsAtZero) {
  audio::AudioEngine engine;
  EXPECT_EQ(engine.stats().event_drops.load(std::memory_order_relaxed), 0u);
  EXPECT_EQ(engine.stats().feedback_drops.load(std::memory_order_relaxed), 0u);
}

// Master gain: a fresh engine is at unity; setMasterGain clamps into
// [0, kMaxMasterGain] (the slider's full scale amplifies, so the ceiling is >1).
TEST(AudioEngineMasterGain, ClampsIntoRange) {
  audio::AudioEngine engine;
  EXPECT_FLOAT_EQ(engine.masterGain(), 1.0f);  // unity by default

  engine.setMasterGain(2.0f);
  EXPECT_FLOAT_EQ(engine.masterGain(), 2.0f);  // in-range value kept verbatim

  engine.setMasterGain(100.0f);
  EXPECT_FLOAT_EQ(engine.masterGain(), audio::kMaxMasterGain);  // clamped to ceiling

  engine.setMasterGain(-1.0f);
  EXPECT_FLOAT_EQ(engine.masterGain(), 0.0f);  // clamped at zero
}
