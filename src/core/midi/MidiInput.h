#ifndef KEYPIANO_CORE_MIDI_MIDIINPUT_H_
#define KEYPIANO_CORE_MIDI_MIDIINPUT_H_

#include "audio/MidiEvent.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class RtMidiIn;  // from <rtmidi/RtMidi.h>, pulled in only by the .cpp

namespace keypiano::midi {

// Decode a raw MIDI message into a MidiEvent. Handles Note On / Note Off (a Note
// On with velocity 0 counts as Note Off, per the MIDI spec) and Control Change.
// Returns nullopt for anything we don't act on (program change, pitch bend,
// sysex, channel/poly pressure, real-time, running status, malformed). Pure and
// dependency-free so it can be unit-tested without an RtMidi device.
std::optional<MidiEvent> decodeMidiMessage(const unsigned char* data,
                                           std::size_t len);

// Thin RtMidi input wrapper: opens one input port and delivers decoded events via
// a callback on RtMidi's own thread. The owner MUST close() it before tearing
// down anything the callback reaches (the audio engine), exactly like the
// keyboard hook — see MainWindow's beforeEngineStop/afterEngineStart wiring.
class MidiInput {
 public:
  using EventFn = std::function<void(const MidiEvent&)>;

  MidiInput();
  ~MidiInput();

  MidiInput(const MidiInput&) = delete;
  MidiInput& operator=(const MidiInput&) = delete;

  // Names of the available MIDI input ports (RtMidi index order).
  static std::vector<std::string> availablePorts();

  // Open the first port whose name contains `name`, delivering events to `cb`.
  // Replaces any currently open port. Returns false on no match / failure.
  bool openByName(const std::string& name, EventFn cb);

  void close();
  bool isOpen() const { return open_; }

 private:
  static void onMessage(double timestamp, std::vector<unsigned char>* message,
                        void* user);

  std::unique_ptr<RtMidiIn> midi_in_;
  EventFn cb_;
  bool    open_ = false;
};

}  // namespace keypiano::midi

#endif  // KEYPIANO_CORE_MIDI_MIDIINPUT_H_
