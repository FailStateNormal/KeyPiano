#include "keymap/KeyMap.h"

#include <algorithm>

namespace keypiano {

// ── ChannelState clamped mutators ─────────────────────────────────────────────

void ChannelState::shiftOctave(int delta) {
  octave_offset = static_cast<int8_t>(
      std::clamp(static_cast<int>(octave_offset) + delta, -4, 4));
}

void ChannelState::shiftVelocity(int delta) {
  velocity = static_cast<uint8_t>(
      std::clamp(static_cast<int>(velocity) + delta, 1, 127));
}

void ChannelState::setVelocityValue(int value) {
  velocity = static_cast<uint8_t>(std::clamp(value, 1, 127));
}

void ChannelState::shiftKeySignature(int delta) {
  key_signature = static_cast<int8_t>(
      std::clamp(static_cast<int>(key_signature) + delta, -6, 6));
}

void KeyMap::addBinding(const KeyBinding& b) {
  auto it = index_.find(b.vk_code);
  if (it != index_.end()) {
    bindings_[it->second] = b;
  } else {
    index_[b.vk_code] = bindings_.size();
    bindings_.push_back(b);
  }
}

void KeyMap::setLabel(uint32_t vk_code, const std::string& label) {
  auto it = index_.find(vk_code);
  if (it != index_.end()) bindings_[it->second].label = label;
}

void KeyMap::removeBinding(uint32_t vk_code) {
  auto it = index_.find(vk_code);
  if (it == index_.end()) return;
  bindings_.erase(bindings_.begin() + static_cast<std::ptrdiff_t>(it->second));
  // Erasing shifts every later element down by one, so rebuild the index map
  // rather than trying to patch individual offsets.
  index_.clear();
  for (std::size_t i = 0; i < bindings_.size(); ++i)
    index_[bindings_[i].vk_code] = i;
}

const KeyBinding* KeyMap::find(uint32_t vk_code) const {
  auto it = index_.find(vk_code);
  if (it == index_.end()) return nullptr;
  return &bindings_[it->second];
}

std::optional<MidiEvent> KeyMap::resolve(uint32_t vk_code, bool is_keydown,
                                          const ChannelState& ch0,
                                          const ChannelState& ch1) const {
  const KeyBinding* b = find(vk_code);
  if (!b) return std::nullopt;

  // Momentary pedals resolve to a ControlChange on both edges: keydown presses
  // the pedal (value 127), keyup releases it (0). The synth (e.g. FluidSynth)
  // implements sustain/sostenuto/soft natively from CC 64/66/67. Octave offset
  // and key signature do not apply — these are channel-wide controls.
  int pedal_cc = -1;
  switch (b->action) {
    case KeyAction::SustainPedal:   pedal_cc = 64; break;
    case KeyAction::SostenutoPedal: pedal_cc = 66; break;
    case KeyAction::SoftPedal:      pedal_cc = 67; break;
    default: break;
  }
  if (pedal_cc >= 0) {
    MidiEvent ev{};
    ev.type  = EventType::ControlChange;
    ev.chan  = b->channel;
    ev.note  = static_cast<uint8_t>(pedal_cc);  // CC number
    ev.vel   = is_keydown ? 127 : 0;            // pedal down / up
    ev.ts_us = 0;  // caller sets the timestamp
    return ev;
  }

  if (b->action != KeyAction::Note) return std::nullopt;

  const ChannelState& ch = (b->channel == 0) ? ch0 : ch1;

  // Apply octave offset and key signature shift, then clamp to 0-127.
  int note = static_cast<int>(b->midi_note)
           + ch.octave_offset * 12
           + ch.key_signature;
  if (note < 0 || note > 127) return std::nullopt;

  MidiEvent ev{};
  ev.chan  = b->channel;
  ev.note = static_cast<uint8_t>(note);
  ev.ts_us = 0;  // caller sets the timestamp

  if (is_keydown) {
    ev.type = EventType::NoteOn;
    ev.vel  = ch.velocity;
  } else {
    ev.type = EventType::NoteOff;
    ev.vel  = 0;
  }
  return ev;
}

KeyResult KeyMap::handle(uint32_t vk_code, bool is_keydown,
                         ChannelState& ch0, ChannelState& ch1) const {
  const KeyBinding* b = find(vk_code);
  if (!b) return {};

  ChannelState& ch = (b->channel == 0) ? ch0 : ch1;

  switch (b->action) {
    case KeyAction::Note:
    case KeyAction::SustainPedal:
    case KeyAction::SostenutoPedal:
    case KeyAction::SoftPedal:
      // Sound-producing actions: delegate to resolve() (which reads channel
      // state but does not mutate it).
      return {resolve(vk_code, is_keydown, ch0, ch1), false};

    case KeyAction::SustainSet:
      // Legacy latch action: emit the CC64 value on press so old maps still
      // actuate the damper (newer maps should use SustainPedal instead).
      if (is_keydown) {
        MidiEvent ev{};
        ev.type  = EventType::ControlChange;
        ev.chan  = b->channel;
        ev.note  = 64;
        ev.vel   = static_cast<uint8_t>(std::clamp<int>(b->step, 0, 127));
        ev.ts_us = 0;
        return {ev, false};
      }
      return {};

    // State changes apply on the leading edge only; the key's release is a no-op
    // (the offset stays until changed again, like FreePiano).
    case KeyAction::OctaveInc:       if (is_keydown) ch.shiftOctave(+b->step);       return {};
    case KeyAction::OctaveDec:       if (is_keydown) ch.shiftOctave(-b->step);       return {};
    case KeyAction::VelocityInc:     if (is_keydown) ch.shiftVelocity(+b->step);     return {};
    case KeyAction::VelocityDec:     if (is_keydown) ch.shiftVelocity(-b->step);     return {};
    case KeyAction::VelocitySet:     if (is_keydown) ch.setVelocityValue(b->step);   return {};
    case KeyAction::KeySignatureInc: if (is_keydown) ch.shiftKeySignature(+b->step); return {};
    case KeyAction::KeySignatureDec: if (is_keydown) ch.shiftKeySignature(-b->step); return {};

    case KeyAction::Record:
      return {std::nullopt, is_keydown};  // toggle on press, ignore release
  }
  return {};
}

}  // namespace keypiano
