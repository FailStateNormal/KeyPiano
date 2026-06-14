#ifndef KEYPIANO_CORE_SYNTH_SYNTHFACTORY_H_
#define KEYPIANO_CORE_SYNTH_SYNTHFACTORY_H_

#include "SynthesizerBase.h"
#include <memory>

namespace keypiano {

std::unique_ptr<SynthesizerBase> createFluidSynth();

// Phase 4: VST3 backend. Only available when built with the `vst3` vcpkg
// feature (KEYPIANO_ENABLE_VST3). Construct empty, then loadInstrument(".vst3").
#ifdef KEYPIANO_ENABLE_VST3
std::unique_ptr<SynthesizerBase> createVst3Synth();
#endif

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_SYNTHFACTORY_H_
