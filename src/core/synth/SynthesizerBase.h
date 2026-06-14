#ifndef KEYPIANO_CORE_SYNTH_SYNTHESIZERBASE_H_
#define KEYPIANO_CORE_SYNTH_SYNTHESIZERBASE_H_

#include <cstdint>
#include <string>

namespace keypiano {

// Pure virtual interface for all synth backends (FluidSynth, VST3, etc.).
//
// Thread-safety contract:
//   noteOn / noteOff / controlChange / allNotesOff / render
//     → audio-thread safe: zero malloc, zero blocking, zero Qt calls.
//   init / shutdown / loadInstrument
//     → must be called from non-audio threads only.
class SynthesizerBase {
 public:
  virtual ~SynthesizerBase() = default;

  virtual bool init(uint32_t sample_rate, uint32_t max_frames) = 0;
  virtual void shutdown() = 0;

  // May perform file I/O — never call from the audio thread.
  virtual bool loadInstrument(const std::string& path) = 0;

  // Audio-thread-safe methods:
  virtual void noteOn(uint8_t ch, uint8_t note, uint8_t vel) = 0;
  virtual void noteOff(uint8_t ch, uint8_t note) = 0;
  virtual void controlChange(uint8_t ch, uint8_t cc, uint8_t val) = 0;
  virtual void allNotesOff() = 0;

  // Mix synthesized audio into `out` (interleaved stereo, `frames` sample-pairs).
  // Adds to the existing buffer content — does NOT zero it first.
  virtual void render(float* out, uint32_t frames) = 0;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_SYNTHESIZERBASE_H_
