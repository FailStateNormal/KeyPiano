#include "synth/SynthFactory.h"
#include "synth/FluidSynthEngine.h"
#ifdef KEYPIANO_ENABLE_VST3
#include "synth/Vst3SynthEngine.h"
#endif

namespace keypiano {

std::unique_ptr<SynthesizerBase> createFluidSynth() {
  return std::make_unique<FluidSynthEngine>();
}

#ifdef KEYPIANO_ENABLE_VST3
std::unique_ptr<SynthesizerBase> createVst3Synth() {
  return std::make_unique<Vst3SynthEngine>();
}
#endif

}  // namespace keypiano
