#ifndef KEYPIANO_CORE_SYNTH_FLUIDSYNTHENGINE_H_
#define KEYPIANO_CORE_SYNTH_FLUIDSYNTHENGINE_H_

#include "SynthesizerBase.h"
#include <fluidsynth.h>
#include <vector>

namespace keypiano {

class FluidSynthEngine final : public SynthesizerBase {
 public:
  FluidSynthEngine();
  ~FluidSynthEngine() override;

  // Non-copyable, non-movable (owns raw FluidSynth handles).
  FluidSynthEngine(const FluidSynthEngine&) = delete;
  FluidSynthEngine& operator=(const FluidSynthEngine&) = delete;

  bool init(uint32_t sample_rate, uint32_t max_frames) override;
  void shutdown() override;
  bool loadInstrument(const std::string& path) override;

  void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override;
  void noteOff(uint8_t ch, uint8_t note) override;
  void controlChange(uint8_t ch, uint8_t cc, uint8_t val) override;
  void allNotesOff() override;
  void render(float* out, uint32_t frames) override;

 private:
  fluid_settings_t* settings_  = nullptr;
  fluid_synth_t*    synth_     = nullptr;
  int               sfont_id_  = FLUID_FAILED;
  uint32_t          max_frames_ = 0;

  // Preallocated in init() so render() never allocates on the audio thread.
  std::vector<float> buf_left_;
  std::vector<float> buf_right_;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_FLUIDSYNTHENGINE_H_
