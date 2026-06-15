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

// ── Pedals (CC 64 / 66 / 67) ─────────────────────────────────────────────────

static const char* kPedals21 = R"(FreePiano 2.1
Keydown Space SustainPedal In_0
Keydown F3 Sostenuto In_0
Keydown F4 Soft In_0
)";

TEST(KeyMapParser, ParsesPedalActions) {
  auto r = parse(kPedals21);
  ASSERT_TRUE(r.ok()) << (!r.errors.empty() ? r.errors[0] : "");
  ASSERT_NE(r.map.find(0x20), nullptr);                    // Space
  EXPECT_EQ(r.map.find(0x20)->action, KeyAction::SustainPedal);
  ASSERT_NE(r.map.find(0x72), nullptr);                    // F3
  EXPECT_EQ(r.map.find(0x72)->action, KeyAction::SostenutoPedal);
  ASSERT_NE(r.map.find(0x73), nullptr);                    // F4
  EXPECT_EQ(r.map.find(0x73)->action, KeyAction::SoftPedal);
}

TEST(KeyMapParser, PedalRejectsMissingChannel) {
  auto r = parse("FreePiano 2.1\nKeydown Space SustainPedal\n");
  EXPECT_FALSE(r.ok());
}

TEST(KeyMap, ResolveSustainPedalDownUp) {
  auto r = parse(kPedals21);
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;

  // Keydown → CC 64 value 127 (pedal pressed).
  auto down = r.map.resolve(0x20, true, ch0, ch1);
  ASSERT_TRUE(down.has_value());
  EXPECT_EQ(down->type, EventType::ControlChange);
  EXPECT_EQ(down->chan, 0);
  EXPECT_EQ(down->note, 64);
  EXPECT_EQ(down->vel, 127);

  // Keyup → CC 64 value 0 (pedal released).
  auto up = r.map.resolve(0x20, false, ch0, ch1);
  ASSERT_TRUE(up.has_value());
  EXPECT_EQ(up->type, EventType::ControlChange);
  EXPECT_EQ(up->note, 64);
  EXPECT_EQ(up->vel, 0);
}

TEST(KeyMap, ResolveSostenutoAndSoftCcNumbers) {
  auto r = parse(kPedals21);
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  EXPECT_EQ(r.map.resolve(0x72, true, ch0, ch1)->note, 66);  // sostenuto
  EXPECT_EQ(r.map.resolve(0x73, true, ch0, ch1)->note, 67);  // soft
}

TEST(KeyMapSerializer, PedalRoundTrip) {
  auto r1 = parse(kPedals21);
  ASSERT_TRUE(r1.ok());
  auto r2 = KeyMapParser::parse(KeyMapSerializer::serialize(r1.map));
  ASSERT_TRUE(r2.ok()) << (!r2.errors.empty() ? r2.errors[0] : "");
  EXPECT_EQ(r2.map.find(0x20)->action, KeyAction::SustainPedal);
  EXPECT_EQ(r2.map.find(0x72)->action, KeyAction::SostenutoPedal);
  EXPECT_EQ(r2.map.find(0x73)->action, KeyAction::SoftPedal);
}

// ── KeyMap::handle (command actions wired up) ────────────────────────────────

TEST(KeyMap, HandleOctaveShiftChangesNote) {
  auto r = parse("FreePiano 2.1\n"
                 "Keydown A Note In_0 C4\n"
                 "Keydown F2 Octave In_0 Inc 1\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  // Before: A → C4 = 60.
  auto a = r.map.handle(0x41, true, ch0, ch1);
  ASSERT_TRUE(a.midi.has_value());
  EXPECT_EQ(a.midi->note, 60);
  // F2 (0x71) raises the octave; A now → C5 = 72.
  r.map.handle(0x71, true, ch0, ch1);
  auto b = r.map.handle(0x41, true, ch0, ch1);
  ASSERT_TRUE(b.midi.has_value());
  EXPECT_EQ(b.midi->note, 72);
}

TEST(KeyMap, HandleVelocityShiftChangesNoteVelocity) {
  auto r = parse("FreePiano 2.1\n"
                 "Keydown A Note In_0 C4\n"
                 "Keydown Tab Velocity In_0 Inc 10\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  const int base = ch0.velocity;
  r.map.handle(0x09, true, ch0, ch1);  // Tab → velocity +10
  auto n = r.map.handle(0x41, true, ch0, ch1);
  ASSERT_TRUE(n.midi.has_value());
  EXPECT_EQ(n.midi->vel, base + 10);
}

TEST(KeyMap, HandleRecordTogglesOnKeydownOnly) {
  auto r = parse("FreePiano 2.1\nKeydown ScrollLock Record\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  EXPECT_TRUE(r.map.handle(0x91, true, ch0, ch1).toggle_record);    // keydown
  EXPECT_FALSE(r.map.handle(0x91, false, ch0, ch1).toggle_record);  // keyup
}

TEST(KeyMap, HandleLegacySustainSetEmitsCc64) {
  auto r = parse("FreePiano 2.1\nKeydown Space Sustain In_0 Set 127\n");
  ASSERT_TRUE(r.ok());
  ChannelState ch0, ch1;
  auto down = r.map.handle(0x20, true, ch0, ch1);
  ASSERT_TRUE(down.midi.has_value());
  EXPECT_EQ(down.midi->type, EventType::ControlChange);
  EXPECT_EQ(down.midi->note, 64);
  EXPECT_EQ(down.midi->vel, 127);
}

TEST(ChannelState, ClampsAllRanges) {
  ChannelState ch;
  for (int i = 0; i < 10; ++i) ch.shiftOctave(+1);
  EXPECT_EQ(ch.octave_offset, 4);
  for (int i = 0; i < 20; ++i) ch.shiftOctave(-1);
  EXPECT_EQ(ch.octave_offset, -4);

  ch.setVelocityValue(999);
  EXPECT_EQ(ch.velocity, 127);
  ch.setVelocityValue(-5);
  EXPECT_EQ(ch.velocity, 1);

  for (int i = 0; i < 20; ++i) ch.shiftKeySignature(+1);
  EXPECT_EQ(ch.key_signature, 6);
  for (int i = 0; i < 40; ++i) ch.shiftKeySignature(-1);
  EXPECT_EQ(ch.key_signature, -6);
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
