#include "keymap/KeyMap.h"

#include <algorithm>

namespace keypiano {

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

}  // namespace keypiano
