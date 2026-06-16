#ifndef KEYPIANO_CORE_SYNTH_INSTRUMENTRESTORE_H_
#define KEYPIANO_CORE_SYNTH_INSTRUMENTRESTORE_H_

#include <string>

namespace keypiano {

// Which backend an instrument snapshot was using. Mirrors the UI's SynthController
// backend enum, kept here (Qt-free) so the restore policy below is unit-testable
// without the GUI or a live audio engine.
enum class SynthBackend { FluidSynth, Vst3 };

// What to restore after a failed backend switch (see planInstrumentRestore).
enum class RestoreTarget {
  ReloadVst3,       // rebuild the VST3 backend and reload the bundle at `path`
  ReloadFluidUser,  // rebuild FluidSynth and reload the user SF2 at `path`
  FallbackDefault,  // no usable user instrument — load the bundled default piano
};

// A snapshot of the instrument that was active before a backend switch, captured
// so the switch can be rolled back if it fails.
struct InstrumentSnapshot {
  SynthBackend backend = SynthBackend::FluidSynth;
  std::string  path;     // SF2 file / VST3 bundle; empty for the built-in default
  bool         builtin = false;  // the instrument was the bundled default
};

// Pure policy: decide what to restore from a snapshot. The actual rebuild/reload
// — and the on-failure fall back to the default — are done by SynthController at
// runtime; this only encodes the *intended target*, so the decision is testable
// in isolation:
//   - a VST3 backend with a remembered bundle  -> reload that VST3
//   - a FluidSynth user SF2 (non-built-in, with a path)  -> reload that SF2
//   - anything else (built-in default, or no remembered path)  -> the default
inline RestoreTarget planInstrumentRestore(const InstrumentSnapshot& s) {
  if (s.backend == SynthBackend::Vst3 && !s.path.empty())
    return RestoreTarget::ReloadVst3;
  if (!s.builtin && !s.path.empty())
    return RestoreTarget::ReloadFluidUser;
  return RestoreTarget::FallbackDefault;
}

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_INSTRUMENTRESTORE_H_
