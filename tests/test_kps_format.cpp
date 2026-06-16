// test_kps_format.cpp — unit tests for KpsFormat (P2-A)

#include "recorder/KpsFormat.h"
#include "audio/MidiEvent.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <climits>

using namespace keypiano;
namespace fs = std::filesystem;

static MidiEvent makeEvent(EventType t, uint8_t chan, uint8_t note, uint8_t vel, int64_t ts) {
  MidiEvent e{};
  e.type = t; e.chan = chan; e.note = note; e.vel = vel; e.ts_us = ts;
  return e;
}

static bool eventsEqual(const MidiEvent& a, const MidiEvent& b) {
  return a.type == b.type && a.chan == b.chan &&
         a.note == b.note && a.vel  == b.vel  && a.ts_us == b.ts_us;
}

class KpsFormatTest : public ::testing::Test {
 protected:
  std::string tmp_;

  void SetUp() override {
    tmp_ = (fs::temp_directory_path() / "keypiano_test_kps.kps").string();
  }
  void TearDown() override {
    std::remove(tmp_.c_str());
  }

  bool write(const KpsMeta& meta, const std::vector<MidiEvent>& evs, std::string* err = nullptr) {
    return KpsFormat::write(tmp_, meta, evs, err);
  }
  bool read(KpsMeta* meta, std::vector<MidiEvent>* evs, std::string* err = nullptr) {
    return KpsFormat::read(tmp_, meta, evs, err);
  }
};

// ── Round-trip ────────────────────────────────────────────────────────────────

TEST_F(KpsFormatTest, RoundTripBasic) {
  KpsMeta meta_in;
  meta_in.title = "Test Recording";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = 1000000;

  std::vector<MidiEvent> events_in = {
    makeEvent(EventType::NoteOn,  0, 60, 80, 0),
    makeEvent(EventType::NoteOff, 0, 60,  0, 500000),
  };

  std::string err;
  ASSERT_TRUE(write(meta_in, events_in, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  EXPECT_EQ(meta_out.title,       meta_in.title);
  EXPECT_EQ(meta_out.created,     meta_in.created);
  EXPECT_EQ(meta_out.duration_us, meta_in.duration_us);
  EXPECT_EQ(meta_out.event_count, 2);

  ASSERT_EQ(events_out.size(), events_in.size());
  for (size_t i = 0; i < events_in.size(); ++i) {
    EXPECT_TRUE(eventsEqual(events_out[i], events_in[i])) << "mismatch at index " << i;
  }
}

// Round-trips through a path with non-ASCII (Chinese) characters. Guards the
// Windows UTF-8 fix: std::ofstream(const char*) interprets the path in the ANSI
// code page (GBK on zh-CN) and would fail on Chinese folders/filenames — common
// because Chinese Windows usernames put Chinese in the temp/AppData path.
// KpsFormat widens UTF-8 → UTF-16 on Windows so the file opens correctly.
TEST(KpsFormatUnicode, ChinesePathRoundTrip) {
  // "测试录音.kps" built from explicit UTF-8 bytes so the test does not depend on
  // the source-file encoding the compiler happens to assume.
  const std::string fname =
      "\xE6\xB5\x8B\xE8\xAF\x95\xE5\xBD\x95\xE9\x9F\xB3.kps";
  const std::string path = fs::temp_directory_path().string() + "/" + fname;
  // fs::path from a UTF-8 string without the C++20-deprecated u8path().
  const fs::path fspath{
      std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())};
  std::error_code ec;
  fs::remove(fspath, ec);  // start clean

  KpsMeta meta_in;
  meta_in.title = "Unicode";
  meta_in.created = "2026-06-16T00:00:00Z";
  meta_in.duration_us = 500000;
  std::vector<MidiEvent> events_in = {
      makeEvent(EventType::NoteOn,  0, 60, 80, 0),
      makeEvent(EventType::NoteOff, 0, 60,  0, 250000),
  };

  std::string err;
  ASSERT_TRUE(KpsFormat::write(path, meta_in, events_in, &err)) << err;
  EXPECT_TRUE(fs::exists(fspath));  // file really lives at the Chinese path

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(KpsFormat::read(path, &meta_out, &events_out, &err)) << err;
  EXPECT_EQ(meta_out.title, meta_in.title);
  ASSERT_EQ(events_out.size(), events_in.size());
  for (size_t i = 0; i < events_in.size(); ++i)
    EXPECT_TRUE(eventsEqual(events_out[i], events_in[i])) << "mismatch at " << i;

  fs::remove(fspath, ec);
}

TEST_F(KpsFormatTest, AllEventTypes) {
  KpsMeta meta_in;
  meta_in.title = "All Types";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = 4000000;

  std::vector<MidiEvent> events_in = {
    makeEvent(EventType::NoteOn,        0, 60,  80, 0),
    makeEvent(EventType::NoteOff,       0, 60,   0, 1000000),
    makeEvent(EventType::ControlChange, 1, 64, 127, 2000000),
    makeEvent(EventType::AllNotesOff,   0,  0,   0, 3000000),
  };

  std::string err;
  ASSERT_TRUE(write(meta_in, events_in, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  ASSERT_EQ(events_out.size(), 4u);
  EXPECT_EQ(events_out[0].type, EventType::NoteOn);
  EXPECT_EQ(events_out[1].type, EventType::NoteOff);
  EXPECT_EQ(events_out[2].type, EventType::ControlChange);
  EXPECT_EQ(events_out[3].type, EventType::AllNotesOff);

  EXPECT_EQ(events_out[2].chan, 1u);
  EXPECT_EQ(events_out[2].note, 64u);
  EXPECT_EQ(events_out[2].vel,  127u);
  EXPECT_EQ(events_out[3].ts_us, 3000000);
}

TEST_F(KpsFormatTest, EmptyEvents) {
  KpsMeta meta_in;
  meta_in.title = "Empty";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = 0;

  std::string err;
  ASSERT_TRUE(write(meta_in, {}, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  EXPECT_EQ(meta_out.title, "Empty");
  EXPECT_EQ(meta_out.event_count, 0);
  EXPECT_TRUE(events_out.empty());
}

TEST_F(KpsFormatTest, LargeTimestamp) {
  KpsMeta meta_in;
  meta_in.title = "Long";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = INT64_MAX;

  std::vector<MidiEvent> events_in = {
    makeEvent(EventType::NoteOn, 0, 60, 80, INT64_MAX),
  };

  std::string err;
  ASSERT_TRUE(write(meta_in, events_in, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  ASSERT_EQ(events_out.size(), 1u);
  EXPECT_EQ(events_out[0].ts_us, INT64_MAX);
  EXPECT_EQ(meta_out.duration_us, INT64_MAX);
}

TEST_F(KpsFormatTest, EventCountMatchesActual) {
  KpsMeta meta_in;
  meta_in.title = "Count Test";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = 600000;

  std::vector<MidiEvent> events_in = {
    makeEvent(EventType::NoteOn,  0, 60, 80, 0),
    makeEvent(EventType::NoteOn,  0, 64, 80, 100000),
    makeEvent(EventType::NoteOff, 0, 60,  0, 500000),
    makeEvent(EventType::NoteOff, 0, 64,  0, 600000),
  };

  std::string err;
  ASSERT_TRUE(write(meta_in, events_in, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  EXPECT_EQ(meta_out.event_count, static_cast<int32_t>(events_in.size()));
  EXPECT_EQ(events_out.size(), events_in.size());
}

TEST_F(KpsFormatTest, TitleWithEqualsSign) {
  KpsMeta meta_in;
  meta_in.title = "My = Cool = Recording";
  meta_in.created = "2026-06-14T00:00:00Z";
  meta_in.duration_us = 0;

  std::string err;
  ASSERT_TRUE(write(meta_in, {}, &err)) << err;

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  ASSERT_TRUE(read(&meta_out, &events_out, &err)) << err;

  EXPECT_EQ(meta_out.title, "My = Cool = Recording");
}

// ── CRLF tolerance ───────────────────────────────────────────────────────────

TEST_F(KpsFormatTest, WindowsLineEndings) {
  {
    std::ofstream f(tmp_, std::ios::binary);
    ASSERT_TRUE(f.is_open());
    f << "# keypiano performance sequence v1\r\n"
         "[meta]\r\n"
         "title = CRLF Test\r\n"
         "created = 2026-06-14T00:00:00Z\r\n"
         "duration_us = 0\r\n"
         "event_count = 1\r\n"
         "[events]\r\n"
         "# ts_us type chan note vel\r\n"
         "0 NoteOn 0 60 80\r\n";
  }

  KpsMeta meta_out;
  std::vector<MidiEvent> events_out;
  std::string err;
  ASSERT_TRUE(KpsFormat::read(tmp_, &meta_out, &events_out, &err)) << err;

  EXPECT_EQ(meta_out.title, "CRLF Test");
  ASSERT_EQ(events_out.size(), 1u);
  EXPECT_EQ(events_out[0].type, EventType::NoteOn);
  EXPECT_EQ(events_out[0].note, 60u);
  EXPECT_EQ(events_out[0].vel,  80u);
  EXPECT_EQ(events_out[0].ts_us, 0);
}

// ── Error cases ───────────────────────────────────────────────────────────────

TEST_F(KpsFormatTest, ReadNonExistentFile) {
  KpsMeta meta;
  std::vector<MidiEvent> events;
  std::string err;
  EXPECT_FALSE(KpsFormat::read(
      (fs::temp_directory_path() / "keypiano_does_not_exist_xyz.kps").string(),
      &meta, &events, &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(KpsFormatTest, WriteFailsBadPath) {
  KpsMeta meta;
  std::vector<MidiEvent> events;
  std::string err;
  std::string bad = (fs::temp_directory_path() /
                     "keypiano_nonexistent_dir_abc_xyz" / "test.kps").string();
  EXPECT_FALSE(KpsFormat::write(bad, meta, events, &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(KpsFormatTest, MalformedEventLine) {
  {
    std::ofstream f(tmp_);
    ASSERT_TRUE(f.is_open());
    f << "# keypiano performance sequence v1\n"
         "[meta]\n"
         "title = Bad\n"
         "created = 2026-06-14T00:00:00Z\n"
         "duration_us = 0\n"
         "event_count = 1\n"
         "[events]\n"
         "not_a_valid_event_line\n";
  }

  KpsMeta meta;
  std::vector<MidiEvent> events;
  std::string err;
  EXPECT_FALSE(KpsFormat::read(tmp_, &meta, &events, &err));
  EXPECT_FALSE(err.empty());
}

TEST_F(KpsFormatTest, UnknownEventType) {
  {
    std::ofstream f(tmp_);
    ASSERT_TRUE(f.is_open());
    f << "[meta]\n"
         "title = Bad\n"
         "created = 2026-06-14T00:00:00Z\n"
         "duration_us = 0\n"
         "event_count = 1\n"
         "[events]\n"
         "0 UnknownType 0 60 80\n";
  }

  KpsMeta meta;
  std::vector<MidiEvent> events;
  std::string err;
  EXPECT_FALSE(KpsFormat::read(tmp_, &meta, &events, &err));
  EXPECT_FALSE(err.empty());
}
