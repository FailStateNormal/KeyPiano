#include "keymap/KeyMapParser.h"
#include "keymap/KeyMapSerializer.h"

#include <gtest/gtest.h>

using namespace keypiano;

// ── Helpers ───────────────────────────────────────────────────────────────────

static ParseResult parse(const char* text) {
  return KeyMapParser::parse(text);
}

// ── Version detection ─────────────────────────────────────────────────────────

TEST(KeyMapParser, UnknownVersion) {
  auto r = parse("unknown version 3.0\nkey a NoteOn 0 c4\n");
  EXPECT_FALSE(r.ok());
}

// ── 2.0 parser ────────────────────────────────────────────────────────────────

static const char* kSample20 = R"(FreePiano 2.0
key a NoteOn 0 c4
key s NoteOn 0 d4
key d NoteOn 0 e4
key w NoteOn 0 c5
keydown up Octave 0 Inc 1
keydown down Octave 0 Dec 1
keydown left KeySignature 0 Dec 1
keydown right KeySignature 0 Inc 1
KeyboardVeolcity 0 Set 100
Sustain 0 Set 127
label a Do
)";

TEST(KeyMapParser, Version20BasicNotes) {
  auto r = parse(kSample20);
  EXPECT_TRUE(r.ok()) << r.errors[0];

  // A → C4 = MIDI 60
  const KeyBinding* b = r.map.find(0x41);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::Note);
  EXPECT_EQ(b->channel, 0);
  EXPECT_EQ(b->midi_note, 60);
}

TEST(KeyMapParser, Version20OctaveInc) {
  auto r = parse(kSample20);
  ASSERT_TRUE(r.ok());
  // VK_UP = 0x26
  const KeyBinding* b = r.map.find(0x26);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::OctaveInc);
  EXPECT_EQ(b->step, 1);
}

TEST(KeyMapParser, Version20KeySigDec) {
  auto r = parse(kSample20);
  ASSERT_TRUE(r.ok());
  // VK_LEFT = 0x25
  const KeyBinding* b = r.map.find(0x25);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::KeySignatureDec);
}

TEST(KeyMapParser, Version20SpellErrorVeolcity) {
  // "KeyboardVeolcity" must be accepted without errors
  auto r = parse(kSample20);
  EXPECT_TRUE(r.ok()) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(KeyMapParser, Version20Label) {
  auto r = parse(kSample20);
  ASSERT_TRUE(r.ok());
  const KeyBinding* b = r.map.find(0x41);  // A
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->label, "Do");
}

// ── 2.1 parser ────────────────────────────────────────────────────────────────

static const char* kSample21 = R"(FreePiano 2.1
Keydown A Note In_0 C4
Keydown S Note In_0 D4
Keydown D Note In_0 E4
Keydown W Note In_0 C5
Keydown F5 Octave In_0 Inc 1
Keydown F6 Octave In_0 Dec 1
Keydown ScrollLock Record
Keydown Tab Velocity In_0 Inc 10
Keydown CapsLock KeySignature In_0 Inc 1
Label A Do
)";

TEST(KeyMapParser, Version21BasicNotes) {
  auto r = parse(kSample21);
  EXPECT_TRUE(r.ok()) << (!r.errors.empty() ? r.errors[0] : "");

  // A → C4 = MIDI 60
  const KeyBinding* b = r.map.find(0x41);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::Note);
  EXPECT_EQ(b->midi_note, 60);
}

TEST(KeyMapParser, Version21Channel) {
  auto r = parse(kSample21);
  ASSERT_TRUE(r.ok());
  const KeyBinding* b = r.map.find(0x41);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->channel, 0);
}

TEST(KeyMapParser, Version21OctaveInc) {
  auto r = parse(kSample21);
  ASSERT_TRUE(r.ok());
  // F5 = 0x74
  const KeyBinding* b = r.map.find(0x74);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::OctaveInc);
  EXPECT_EQ(b->step, 1);
}

TEST(KeyMapParser, Version21Record) {
  auto r = parse(kSample21);
  ASSERT_TRUE(r.ok());
  // ScrollLock = 0x91
  const KeyBinding* b = r.map.find(0x91);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::Record);
}

TEST(KeyMapParser, Version21VelocityInc) {
  auto r = parse(kSample21);
  ASSERT_TRUE(r.ok());
  // Tab = 0x09
  const KeyBinding* b = r.map.find(0x09);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::VelocityInc);
  EXPECT_EQ(b->step, 10);
}

TEST(KeyMapParser, Version21Label) {
  auto r = parse(kSample21);
  ASSERT_TRUE(r.ok());
  const KeyBinding* b = r.map.find(0x41);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->label, "Do");
}

// ── MIDI note parsing ─────────────────────────────────────────────────────────

TEST(KeyMapParser, NoteNames) {
  // C4 = 60, A4 = 69, F#5 = 78, Eb3 = 51
  auto r = KeyMapParser::parse("FreePiano 2.1\n"
    "Keydown A Note In_0 C4\n"
    "Keydown B Note In_0 A4\n"
    "Keydown C Note In_0 F#5\n"
    "Keydown D Note In_0 Eb3\n");
  ASSERT_TRUE(r.ok()) << (!r.errors.empty() ? r.errors[0] : "");
  EXPECT_EQ(r.map.find(0x41)->midi_note, 60);
  EXPECT_EQ(r.map.find(0x42)->midi_note, 69);
  EXPECT_EQ(r.map.find(0x43)->midi_note, 78);
  EXPECT_EQ(r.map.find(0x44)->midi_note, 51);
}

// ── KeyMap::resolve ───────────────────────────────────────────────────────────

TEST(KeyMap, ResolveNoteOn) {
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C4\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  auto ev = r.map.resolve(0x41, true, ch0, ch1);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->type, EventType::NoteOn);
  EXPECT_EQ(ev->note, 60);
  EXPECT_EQ(ev->vel, ch0.velocity);
}

TEST(KeyMap, ResolveNoteOff) {
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C4\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  auto ev = r.map.resolve(0x41, false, ch0, ch1);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->type, EventType::NoteOff);
}

TEST(KeyMap, ResolveOctaveShift) {
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C4\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  ch0.octave_offset = 1;
  auto ev = r.map.resolve(0x41, true, ch0, ch1);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->note, 72);  // C5
}

TEST(KeyMap, ResolveKeySignatureShift) {
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C4\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  ch0.key_signature = 2;
  auto ev = r.map.resolve(0x41, true, ch0, ch1);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->note, 62);  // D4
}

TEST(KeyMap, ResolveOutOfRangeClamped) {
  // C-1 + octave -1 = note -12 → should return nullopt
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C-1\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  ch0.octave_offset = -1;
  auto ev = r.map.resolve(0x41, true, ch0, ch1);
  EXPECT_FALSE(ev.has_value());
}

TEST(KeyMap, ResolveUnmappedKey) {
  auto r = parse("FreePiano 2.1\nKeydown A Note In_0 C4\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  EXPECT_FALSE(r.map.resolve(0x42, true, ch0, ch1).has_value());  // B not mapped
}

// ── Round-trip (serialize → parse) ───────────────────────────────────────────

TEST(KeyMapSerializer, RoundTrip) {
  auto r1 = parse(kSample21);
  ASSERT_TRUE(r1.ok());
  std::string serialized = KeyMapSerializer::serialize(r1.map);

  auto r2 = KeyMapParser::parse(serialized);
  EXPECT_TRUE(r2.ok()) << (!r2.errors.empty() ? r2.errors[0] : "");

  // Same number of bindings
  EXPECT_EQ(r1.map.bindings().size(), r2.map.bindings().size());

  // Spot-check A still maps to C4 NoteOn
  const KeyBinding* b = r2.map.find(0x41);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->action, KeyAction::Note);
  EXPECT_EQ(b->midi_note, 60);
}
