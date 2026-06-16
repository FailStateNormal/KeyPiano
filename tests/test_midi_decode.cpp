// test_midi_decode.cpp — verifies raw-MIDI-byte → MidiEvent decoding. Pure logic,
// no RtMidi device needed (decodeMidiMessage is dependency-free).

#include "midi/MidiInput.h"

#include <gtest/gtest.h>

using namespace keypiano;
using keypiano::midi::decodeMidiMessage;

TEST(MidiDecode, NoteOn) {
  unsigned char m[] = {0x90, 60, 100};
  auto e = decodeMidiMessage(m, 3);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->type, EventType::NoteOn);
  EXPECT_EQ(int(e->chan), 0);
  EXPECT_EQ(int(e->note), 60);
  EXPECT_EQ(int(e->vel), 100);
}

TEST(MidiDecode, NoteOnVelocityZeroIsNoteOff) {
  unsigned char m[] = {0x90, 60, 0};
  auto e = decodeMidiMessage(m, 3);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->type, EventType::NoteOff);
  EXPECT_EQ(int(e->note), 60);
}

TEST(MidiDecode, NoteOff) {
  unsigned char m[] = {0x80, 62, 64};
  auto e = decodeMidiMessage(m, 3);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->type, EventType::NoteOff);
  EXPECT_EQ(int(e->note), 62);
}

TEST(MidiDecode, ControlChangeKeepsChannelAndData) {
  unsigned char m[] = {0xB5, 64, 127};  // channel 5, sustain CC 64 = 127
  auto e = decodeMidiMessage(m, 3);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->type, EventType::ControlChange);
  EXPECT_EQ(int(e->chan), 5);
  EXPECT_EQ(int(e->note), 64);  // CC number rides in note
  EXPECT_EQ(int(e->vel), 127);  // CC value rides in vel
}

TEST(MidiDecode, IgnoresUnhandledAndMalformed) {
  unsigned char prog[] = {0xC0, 5};            // program change — ignored
  EXPECT_FALSE(decodeMidiMessage(prog, 2).has_value());
  unsigned char pitch[] = {0xE0, 0, 64};       // pitch bend — ignored
  EXPECT_FALSE(decodeMidiMessage(pitch, 3).has_value());
  unsigned char short_on[] = {0x90, 60};       // Note On missing velocity
  EXPECT_FALSE(decodeMidiMessage(short_on, 2).has_value());
  EXPECT_FALSE(decodeMidiMessage(nullptr, 0).has_value());
}
