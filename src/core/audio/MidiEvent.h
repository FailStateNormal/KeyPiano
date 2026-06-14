#ifndef KEYPIANO_CORE_AUDIO_MIDIEVENT_H_
#define KEYPIANO_CORE_AUDIO_MIDIEVENT_H_

#include <cstdint>

namespace keypiano {

enum class EventType : uint8_t {
  NoteOn = 0,
  NoteOff = 1,
  ControlChange = 2,
  AllNotesOff = 3,
};

struct MidiEvent {
  EventType type;
  uint8_t   chan;    // MIDI channel 0-15
  uint8_t   note;    // MIDI note number 0-127
  uint8_t   vel;     // velocity 0-127
  int64_t   ts_us;   // timestamp in microseconds since recording start
};

// Layout on x64: 4 single-byte fields, then 4 bytes of padding so the 8-byte
// ts_us is naturally aligned -> 16 bytes total. Kept small and POD so the
// lock-free queues can copy it trivially.
static_assert(sizeof(MidiEvent) == 16, "MidiEvent size mismatch");

}  // namespace keypiano

#endif  // KEYPIANO_CORE_AUDIO_MIDIEVENT_H_
