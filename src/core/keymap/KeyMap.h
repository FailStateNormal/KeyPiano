#ifndef KEYPIANO_CORE_KEYMAP_KEYMAP_H_
#define KEYPIANO_CORE_KEYMAP_KEYMAP_H_

#include "audio/MidiEvent.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace keypiano {

// What a key binding does when triggered.
enum class KeyAction : uint8_t {
  Note,           // send NoteOn / NoteOff
  OctaveInc,      // octave_offset += step
  OctaveDec,      // octave_offset -= step  (step stored as positive)
  VelocityInc,    // velocity += step
  VelocityDec,
  KeySignatureInc,  // key_signature += step
  KeySignatureDec,
  SustainSet,     // set sustain to `step` (0 or 127) — legacy latch, state only
  VelocitySet,    // set velocity to `step`
  Record,         // toggle recording (no MIDI event)

  // Momentary pedals. Each resolves to a MIDI ControlChange on BOTH key edges:
  // keydown presses the pedal (CC value 127), keyup releases it (0). The
  // synthesizer implements the actual behavior from the standard CC numbers.
  SustainPedal,    // CC 64 — sustain / damper: held notes ring until released
  SostenutoPedal,  // CC 66 — sostenuto: sustains only the notes held when pressed
  SoftPedal,       // CC 67 — soft / una corda: softens subsequent notes
};

struct KeyBinding {
  uint32_t    vk_code  = 0;
  KeyAction   action   = KeyAction::Note;
  uint8_t     channel  = 0;    // 0 = In_0 / ch0, 1 = In_1 / ch1
  uint8_t     midi_note = 60;  // only meaningful when action == Note
  int8_t      step     = 1;    // Inc/Dec magnitude or Set value
  std::string label;           // overlay text shown in PianoWidget (may be empty)
};

// Per-channel mutable state — these are owned by the application layer and
// passed into KeyMap::resolve() so the KeyMap itself stays stateless.
struct ChannelState {
  int8_t  octave_offset = 0;    // -4 … +4
  uint8_t velocity      = 80;   // 1-127
  uint8_t sustain       = 127;  // 0 or 127

  // key_signature lives here (not in KeyMap) so it can be changed at runtime
  // without reloading the map.  Range: -6 … +6 semitones.
  int8_t  key_signature = 0;

  // Clamped mutators — the single place range limits live, so no caller ever
  // has to re-clamp octave/velocity/key-signature changes by hand.
  void shiftOctave(int delta);        // result clamped to [-4, +4]
  void shiftVelocity(int delta);      // result clamped to [1, 127]
  void setVelocityValue(int value);   // clamped to [1, 127]
  void shiftKeySignature(int delta);  // result clamped to [-6, +6]
};

// Outcome of feeding one key event through a KeyMap. A binding produces at most
// one of: a MIDI event to send, or a request to toggle recording. Octave /
// velocity / key-signature actions mutate the ChannelState in place and produce
// neither (they change how *future* notes resolve).
struct KeyResult {
  std::optional<MidiEvent> midi;          // Note / ControlChange to dispatch
  bool                     toggle_record = false;  // Record action fired
};

class KeyMap {
 public:
  std::string name;

  // Add or overwrite a binding. Keyed by vk_code.
  void addBinding(const KeyBinding& b);

  // Look up `vk_code` and compute the resulting MidiEvent (if any).
  // `ch0` / `ch1` are passed in (not stored) so they remain the caller's
  // responsibility.  Returns nullopt for non-Note actions (the caller must
  // apply the state mutation itself) or if the key is not mapped.
  std::optional<MidiEvent> resolve(uint32_t vk_code, bool is_keydown,
                                   const ChannelState& ch0,
                                   const ChannelState& ch1) const;

  // Full key handling for the input layer: resolves Note/pedal/legacy-sustain to
  // a MidiEvent, applies octave/velocity/key-signature actions to the matching
  // ChannelState (clamped), and reports Record toggles. `ch0`/`ch1` are mutated
  // in place for the state-changing actions. `*this` is not modified, so it is
  // safe to call on a shared immutable snapshot from the input thread.
  KeyResult handle(uint32_t vk_code, bool is_keydown,
                   ChannelState& ch0, ChannelState& ch1) const;

  // Return the binding for a key, or nullptr if not mapped.
  const KeyBinding* find(uint32_t vk_code) const;

  // Update the label of an existing binding. No-op if vk_code is not mapped.
  void setLabel(uint32_t vk_code, const std::string& label);

  // Remove the binding for `vk_code`. No-op if not mapped.
  void removeBinding(uint32_t vk_code);

  const std::vector<KeyBinding>& bindings() const { return bindings_; }

 private:
  std::vector<KeyBinding> bindings_;
  std::unordered_map<uint32_t, std::size_t> index_;  // vk_code → bindings_ idx
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_KEYMAP_KEYMAP_H_
