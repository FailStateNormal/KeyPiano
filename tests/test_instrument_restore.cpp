// test_instrument_restore.cpp — the pure rollback policy used when a backend
// switch fails (P7-3 ②). planInstrumentRestore() is Qt-free and engine-free, so
// the decision is unit-tested here in isolation; SynthController::restore() drives
// the actual rebuild/reload from this same policy.

#include "synth/InstrumentRestore.h"

#include <gtest/gtest.h>

using namespace keypiano;

// A VST3 backend with a remembered bundle path reloads that VST3.
TEST(InstrumentRestore, Vst3WithPathReloadsVst3) {
  InstrumentSnapshot s{SynthBackend::Vst3, "C:/plugins/Piano.vst3", false};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::ReloadVst3);
}

// A VST3 backend with no remembered path can't be reloaded — fall back.
TEST(InstrumentRestore, Vst3WithoutPathFallsBack) {
  InstrumentSnapshot s{SynthBackend::Vst3, "", false};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::FallbackDefault);
}

// A FluidSynth user SF2 (not the built-in, with a path) reloads that SF2.
TEST(InstrumentRestore, FluidUserSf2ReloadsUser) {
  InstrumentSnapshot s{SynthBackend::FluidSynth, "C:/fonts/Grand.sf2", false};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::ReloadFluidUser);
}

// The bundled default (built-in) always restores to the default, even if a path
// happens to be set.
TEST(InstrumentRestore, BuiltinFallsBackToDefault) {
  InstrumentSnapshot s{SynthBackend::FluidSynth, "C:/fonts/GeneralUser-GS.sf2",
                       true};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::FallbackDefault);
}

// FluidSynth with no remembered path (e.g. the built-in with an empty path) falls
// back to the default.
TEST(InstrumentRestore, FluidWithoutPathFallsBack) {
  InstrumentSnapshot s{SynthBackend::FluidSynth, "", false};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::FallbackDefault);
}

// The VST3 branch takes precedence over the built-in flag: a VST3 with a path is
// reloaded as VST3 regardless of `builtin` (VST3 is never the bundled default).
TEST(InstrumentRestore, Vst3PathBeatsBuiltinFlag) {
  InstrumentSnapshot s{SynthBackend::Vst3, "C:/plugins/Synth.vst3", true};
  EXPECT_EQ(planInstrumentRestore(s), RestoreTarget::ReloadVst3);
}
