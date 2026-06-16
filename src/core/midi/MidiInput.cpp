#include "midi/MidiInput.h"

#include <rtmidi/RtMidi.h>

namespace keypiano::midi {

std::optional<MidiEvent> decodeMidiMessage(const unsigned char* data,
                                           std::size_t len) {
  if (data == nullptr || len < 2) return std::nullopt;

  const unsigned char status = data[0] & 0xF0;
  const uint8_t channel = static_cast<uint8_t>(data[0] & 0x0F);

  switch (status) {
    case 0x90:  // Note On — velocity 0 is a Note Off by convention
      if (len < 3) return std::nullopt;
      if (data[2] == 0)
        return MidiEvent{EventType::NoteOff, channel, data[1], 0, 0};
      return MidiEvent{EventType::NoteOn, channel, data[1], data[2], 0};
    case 0x80:  // Note Off
      if (len < 3) return std::nullopt;
      return MidiEvent{EventType::NoteOff, channel, data[1], 0, 0};
    case 0xB0:  // Control Change (includes sustain CC64 etc.)
      if (len < 3) return std::nullopt;
      return MidiEvent{EventType::ControlChange, channel, data[1], data[2], 0};
    default:
      return std::nullopt;  // program/pitch/pressure/sysex/realtime — ignored
  }
}

MidiInput::MidiInput() {
  try {
    midi_in_ = std::make_unique<RtMidiIn>();
  } catch (...) {
    midi_in_.reset();  // no MIDI API available — availablePorts() will be empty
  }
}

MidiInput::~MidiInput() { close(); }

std::vector<std::string> MidiInput::availablePorts() {
  std::vector<std::string> names;
  try {
    RtMidiIn probe;
    const unsigned n = probe.getPortCount();
    for (unsigned i = 0; i < n; ++i) names.push_back(probe.getPortName(i));
  } catch (...) {
  }
  return names;
}

bool MidiInput::openByName(const std::string& name, EventFn cb) {
  close();
  if (!midi_in_ || name.empty()) return false;
  try {
    const unsigned n = midi_in_->getPortCount();
    for (unsigned i = 0; i < n; ++i) {
      if (midi_in_->getPortName(i).find(name) != std::string::npos) {
        cb_ = std::move(cb);
        midi_in_->setCallback(&MidiInput::onMessage, this);
        // We only want note/CC — drop sysex, timing, and active-sensing.
        midi_in_->ignoreTypes(true, true, true);
        midi_in_->openPort(i);
        open_ = true;
        return true;
      }
    }
  } catch (...) {
    open_ = false;
  }
  return false;
}

void MidiInput::close() {
  if (!midi_in_) {
    open_ = false;
    return;
  }
  try {
    if (open_) {
      midi_in_->cancelCallback();
      midi_in_->closePort();
    }
  } catch (...) {
  }
  open_ = false;
  cb_ = nullptr;
}

void MidiInput::onMessage(double /*timestamp*/,
                          std::vector<unsigned char>* message, void* user) {
  auto* self = static_cast<MidiInput*>(user);
  if (self == nullptr || message == nullptr || !self->cb_) return;
  if (auto ev = decodeMidiMessage(message->data(), message->size()))
    self->cb_(*ev);
}

}  // namespace keypiano::midi
