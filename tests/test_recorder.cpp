// test_recorder.cpp — unit tests for Recorder (P2-B)
//
// We avoid a real AudioEngine (requires WASAPI) by using a DispatchFn lambda
// that captures received events into a vector. Timing tests use 100 ms intervals
// with ±40 ms tolerance to accommodate Windows timer resolution (~15 ms).

#include "recorder/Recorder.h"
#include "audio/MidiEvent.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <vector>

using namespace keypiano;
namespace fs = std::filesystem;
using State = Recorder::State;
using ms = std::chrono::milliseconds;

// ── Helpers ───────────────────────────────────────────────────────────────────

static MidiEvent makeEvent(EventType t, uint8_t chan, uint8_t note, uint8_t vel,
                            int64_t ts_us) {
  MidiEvent e{};
  e.type = t; e.chan = chan; e.note = note; e.vel = vel; e.ts_us = ts_us;
  return e;
}

// Thread-safe sink that records dispatched events and wall-clock arrival times.
struct Sink {
  std::mutex                                        mu;
  std::vector<MidiEvent>                            events;
  std::vector<std::chrono::steady_clock::time_point> times;

  Recorder::DispatchFn fn() {
    return [this](const MidiEvent& e) {
      std::lock_guard<std::mutex> lk(mu);
      events.push_back(e);
      times.push_back(std::chrono::steady_clock::now());
    };
  }
};

// Small helper: busy-wait until state == expected or timeout.
static bool waitForState(const Recorder& r, State expected,
                          std::chrono::milliseconds timeout = ms{500}) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (r.state() == expected) return true;
    std::this_thread::sleep_for(ms{5});
  }
  return false;
}

// "Twinkle Twinkle" first eight notes at 0 ms spacing (instant, no sleep).
static std::vector<MidiEvent> twinkleInstant() {
  // C C G G A A G  (NoteOn + NoteOff pairs, all at ts_us = 0 for speed)
  const uint8_t notes[] = {60, 60, 67, 67, 69, 69, 67};
  std::vector<MidiEvent> evs;
  for (uint8_t n : notes) {
    evs.push_back(makeEvent(EventType::NoteOn,  0, n, 80, 0));
    evs.push_back(makeEvent(EventType::NoteOff, 0, n,  0, 0));
  }
  return evs;
}

// "Twinkle Twinkle" with 100 ms per note (for timing tests).
static std::vector<MidiEvent> twinkleTimed() {
  const uint8_t notes[] = {60, 60, 67, 67, 69, 69, 67};
  std::vector<MidiEvent> evs;
  int64_t t = 0;
  for (uint8_t n : notes) {
    evs.push_back(makeEvent(EventType::NoteOn,  0, n, 80, t));
    evs.push_back(makeEvent(EventType::NoteOff, 0, n,  0, t + 50'000));
    t += 100'000;  // 100 ms
  }
  return evs;
}

// ── State machine ─────────────────────────────────────────────────────────────

TEST(Recorder, InitialStateIsIdle) {
  Recorder r([](const MidiEvent&) {}, 16);
  EXPECT_EQ(r.state(), State::Idle);
  EXPECT_EQ(r.eventCount(), 0u);
}

TEST(Recorder, StartRecordingFromIdle) {
  Recorder r([](const MidiEvent&) {}, 16);
  EXPECT_TRUE(r.startRecording());
  EXPECT_EQ(r.state(), State::Recording);
}

TEST(Recorder, StartRecordingFailsWhenNotIdle) {
  Recorder r([](const MidiEvent&) {}, 16);
  ASSERT_TRUE(r.startRecording());
  EXPECT_FALSE(r.startRecording());  // already Recording
}

TEST(Recorder, StopRecording) {
  Recorder r([](const MidiEvent&) {}, 16);
  ASSERT_TRUE(r.startRecording());
  EXPECT_TRUE(r.stopRecording());
  EXPECT_EQ(r.state(), State::Idle);
}

TEST(Recorder, StopRecordingFailsWhenIdle) {
  Recorder r([](const MidiEvent&) {}, 16);
  EXPECT_FALSE(r.stopRecording());
}

TEST(Recorder, StartPlaybackFailsWithNoEvents) {
  Recorder r([](const MidiEvent&) {}, 16);
  EXPECT_FALSE(r.startPlayback());
}

TEST(Recorder, StartPlaybackFailsWhenRecording) {
  Recorder r([](const MidiEvent&) {}, 16);
  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  EXPECT_FALSE(r.startPlayback());
  r.stopRecording();
}

// ── Recording ─────────────────────────────────────────────────────────────────

TEST(Recorder, OnMidiEventIgnoredWhenIdle) {
  Recorder r([](const MidiEvent&) {}, 16);
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  EXPECT_EQ(r.eventCount(), 0u);
}

TEST(Recorder, RecordingStoresEvents) {
  Recorder r([](const MidiEvent&) {}, 16);
  ASSERT_TRUE(r.startRecording());
  auto evs = twinkleInstant();
  for (const auto& e : evs) r.onMidiEvent(e);
  ASSERT_TRUE(r.stopRecording());
  EXPECT_EQ(r.eventCount(), evs.size());
}

TEST(Recorder, RecordingResetsBufferOnRestart) {
  Recorder r([](const MidiEvent&) {}, 16);
  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 64, 80, 0));
  ASSERT_TRUE(r.stopRecording());
  EXPECT_EQ(r.eventCount(), 2u);

  // Re-record — should start fresh.
  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 72, 80, 0));
  ASSERT_TRUE(r.stopRecording());
  EXPECT_EQ(r.eventCount(), 1u);
}

TEST(Recorder, CapacityLimitEnforced) {
  const size_t cap = 4;
  Recorder r([](const MidiEvent&) {}, cap);
  ASSERT_TRUE(r.startRecording());
  for (size_t i = 0; i < cap + 3; ++i)
    r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  ASSERT_TRUE(r.stopRecording());
  EXPECT_EQ(r.eventCount(), cap);  // capped at max_events
}

// ── Save / Load ───────────────────────────────────────────────────────────────

class RecorderFileTest : public ::testing::Test {
 protected:
  std::string tmp_;
  void SetUp() override {
    tmp_ = (fs::temp_directory_path() / "keypiano_test_recorder.kps").string();
  }
  void TearDown() override { std::remove(tmp_.c_str()); }
};

TEST_F(RecorderFileTest, SaveAndLoadRoundTrip) {
  auto src_evs = twinkleInstant();

  // Record
  Recorder r([](const MidiEvent&) {}, 64);
  ASSERT_TRUE(r.startRecording());
  for (const auto& e : src_evs) r.onMidiEvent(e);
  ASSERT_TRUE(r.stopRecording());

  // Save
  KpsMeta meta_out;
  meta_out.title = "小星星测试";
  meta_out.created = "2026-06-14T00:00:00Z";
  meta_out.duration_us = 0;
  std::string err;
  ASSERT_TRUE(r.saveToFile(tmp_, meta_out, &err)) << err;

  // Load into a fresh Recorder
  Recorder r2([](const MidiEvent&) {}, 64);
  KpsMeta meta_in;
  ASSERT_TRUE(r2.loadFromFile(tmp_, &meta_in, &err)) << err;

  EXPECT_EQ(meta_in.title, "小星星测试");
  EXPECT_EQ(r2.eventCount(), src_evs.size());

  // Cannot check internal events directly, but playback will verify them.
}

TEST_F(RecorderFileTest, LoadFailsOnMissingFile) {
  Recorder r([](const MidiEvent&) {}, 16);
  KpsMeta meta;
  std::string err;
  EXPECT_FALSE(r.loadFromFile("/nonexistent_xyz/test.kps", &meta, &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(RecorderFileTest, LoadFailsIfExceedsCapacity) {
  // Write a file with many events.
  auto evs = twinkleInstant();  // 14 events
  KpsMeta m;
  m.title = "x"; m.created = "2026-06-14T00:00:00Z"; m.duration_us = 0;
  std::string err;
  ASSERT_TRUE(KpsFormat::write(tmp_, m, evs, &err)) << err;

  // Load into a Recorder with capacity 4 — should fail.
  Recorder r([](const MidiEvent&) {}, 4);
  KpsMeta meta;
  EXPECT_FALSE(r.loadFromFile(tmp_, &meta, &err));
  EXPECT_FALSE(err.empty());
}

// ── Playback (instant events, ts_us = 0) ─────────────────────────────────────

TEST(Recorder, PlaybackDispatchesAllEvents) {
  auto src_evs = twinkleInstant();
  Sink sink;

  Recorder r(sink.fn(), 64);
  ASSERT_TRUE(r.startRecording());
  for (const auto& e : src_evs) r.onMidiEvent(e);
  ASSERT_TRUE(r.stopRecording());

  ASSERT_TRUE(r.startPlayback());
  EXPECT_EQ(r.state(), State::Playing);
  ASSERT_TRUE(waitForState(r, State::Idle, ms{2000}))
      << "Playback did not finish";

  std::lock_guard<std::mutex> lk(sink.mu);
  EXPECT_EQ(sink.events.size(), src_evs.size());
}

TEST(Recorder, PlaybackDispatchesInOrder) {
  Sink sink;
  Recorder r(sink.fn(), 64);

  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn,  0, 60, 80, 0));
  r.onMidiEvent(makeEvent(EventType::NoteOn,  0, 64, 75, 0));
  r.onMidiEvent(makeEvent(EventType::NoteOff, 0, 60,  0, 0));
  r.onMidiEvent(makeEvent(EventType::NoteOff, 0, 64,  0, 0));
  ASSERT_TRUE(r.stopRecording());

  ASSERT_TRUE(r.startPlayback());
  ASSERT_TRUE(waitForState(r, State::Idle, ms{2000}));

  std::lock_guard<std::mutex> lk(sink.mu);
  ASSERT_EQ(sink.events.size(), 4u);
  EXPECT_EQ(sink.events[0].type, EventType::NoteOn);
  EXPECT_EQ(sink.events[0].note, 60u);
  EXPECT_EQ(sink.events[1].type, EventType::NoteOn);
  EXPECT_EQ(sink.events[1].note, 64u);
  EXPECT_EQ(sink.events[2].type, EventType::NoteOff);
  EXPECT_EQ(sink.events[3].type, EventType::NoteOff);
}

TEST(Recorder, PlaybackTransitionsToIdleWhenDone) {
  Sink sink;
  Recorder r(sink.fn(), 16);

  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  ASSERT_TRUE(r.stopRecording());

  ASSERT_TRUE(r.startPlayback());
  ASSERT_TRUE(waitForState(r, State::Idle, ms{2000}));
  EXPECT_EQ(r.state(), State::Idle);
}

TEST(Recorder, PlaybackCanBeAborted) {
  Sink sink;
  Recorder r(sink.fn(), 16);

  // Use long timestamps so the playback thread is asleep when we abort.
  ASSERT_TRUE(r.startRecording());
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80, 0));
  r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 64, 80, 5'000'000));  // 5 s later
  ASSERT_TRUE(r.stopRecording());

  ASSERT_TRUE(r.startPlayback());
  std::this_thread::sleep_for(ms{50});  // let first event dispatch
  r.stopPlayback();                     // abort before the 5 s event

  EXPECT_EQ(r.state(), State::Idle);
  std::lock_guard<std::mutex> lk(sink.mu);
  EXPECT_LT(sink.events.size(), 2u);   // second event was never dispatched
}

// ── Timing verification ───────────────────────────────────────────────────────
//
// Uses 100 ms intervals and ±40 ms tolerance to accommodate Windows timer
// jitter (~15 ms). Total test time: ~600 ms.

TEST(Recorder, PlaybackTimingApprox) {
  Sink sink;
  Recorder r(sink.fn(), 64);

  // NoteOn at 0, 100 ms, 200 ms, 300 ms, 400 ms, 500 ms
  ASSERT_TRUE(r.startRecording());
  for (int i = 0; i < 6; ++i)
    r.onMidiEvent(makeEvent(EventType::NoteOn, 0, 60, 80,
                             static_cast<int64_t>(i) * 100'000));
  ASSERT_TRUE(r.stopRecording());

  ASSERT_TRUE(r.startPlayback());
  ASSERT_TRUE(waitForState(r, State::Idle, ms{3000}))
      << "Playback timed out";

  std::lock_guard<std::mutex> lk(sink.mu);
  ASSERT_EQ(sink.times.size(), 6u);

  auto t0 = sink.times[0];
  constexpr double tolerance_ms = 60.0;  // Windows timer resolution ~15 ms

  for (int i = 1; i < 6; ++i) {
    double actual_ms = static_cast<double>(
        std::chrono::duration_cast<ms>(sink.times[i] - t0).count());
    double expected_ms = i * 100.0;
    EXPECT_NEAR(actual_ms, expected_ms, tolerance_ms)
        << "Event " << i << " arrived at " << actual_ms
        << " ms, expected ~" << expected_ms << " ms";
  }
}

// ── Full scenario: 小星星 record → save → load → playback ────────────────────

class TwinkleStarTest : public ::testing::Test {
 protected:
  std::string tmp_;
  void SetUp() override {
    tmp_ = (fs::temp_directory_path() / "keypiano_twinkle.kps").string();
  }
  void TearDown() override { std::remove(tmp_.c_str()); }
};

TEST_F(TwinkleStarTest, FullScenario) {
  auto src_evs = twinkleTimed();  // 14 events at 100 ms / note

  // ── Step 1: Record ─────────────────────────────────────────────────────────
  Sink rec_sink;
  Recorder recorder(rec_sink.fn(), 128);

  ASSERT_TRUE(recorder.startRecording());
  for (const auto& e : src_evs) recorder.onMidiEvent(e);
  ASSERT_TRUE(recorder.stopRecording());
  EXPECT_EQ(recorder.eventCount(), src_evs.size());

  // ── Step 2: Save ───────────────────────────────────────────────────────────
  KpsMeta meta_write;
  meta_write.title = "小星星";
  meta_write.created = "2026-06-14T00:00:00Z";
  meta_write.duration_us = src_evs.back().ts_us + 50'000;

  std::string err;
  ASSERT_TRUE(recorder.saveToFile(tmp_, meta_write, &err)) << err;

  // ── Step 3: Load into a fresh Recorder ────────────────────────────────────
  Sink play_sink;
  Recorder player(play_sink.fn(), 128);

  KpsMeta meta_read;
  ASSERT_TRUE(player.loadFromFile(tmp_, &meta_read, &err)) << err;

  EXPECT_EQ(meta_read.title, "小星星");
  EXPECT_EQ(player.eventCount(), src_evs.size());

  // ── Step 4: Playback and verify dispatch order ─────────────────────────────
  ASSERT_TRUE(player.startPlayback());
  // src_evs span ~650 ms (7 notes × 100 ms); wait up to 3 s.
  ASSERT_TRUE(waitForState(player, State::Idle, ms{3000}))
      << "Playback did not finish within 3 s";

  std::lock_guard<std::mutex> lk(play_sink.mu);
  ASSERT_EQ(play_sink.events.size(), src_evs.size());

  // Verify event types and notes in order.
  for (size_t i = 0; i < src_evs.size(); ++i) {
    EXPECT_EQ(play_sink.events[i].type, src_evs[i].type) << "i=" << i;
    EXPECT_EQ(play_sink.events[i].note, src_evs[i].note) << "i=" << i;
    EXPECT_EQ(play_sink.events[i].vel,  src_evs[i].vel)  << "i=" << i;
  }
}
