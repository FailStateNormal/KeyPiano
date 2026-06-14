#ifndef KEYPIANO_CORE_SYNTH_VST3SYNTHENGINE_H_
#define KEYPIANO_CORE_SYNTH_VST3SYNTHENGINE_H_

#include "IPluginEditor.h"
#include "SynthesizerBase.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace keypiano {

// VST3 instrument host backend implementing SynthesizerBase.
//
// Threading contract (same as the base interface):
//   noteOn / noteOff / controlChange / allNotesOff / render
//     → audio-thread safe: zero malloc, zero blocking, zero locks. Events are
//       accumulated into a preallocated EventList and submitted in render().
//   init / shutdown / loadInstrument
//     → non-audio threads only (file I/O, plugin activation, buffer alloc).
//
// Usage mirrors FluidSynthEngine: construct empty, init(sr, frames) negotiates
// the format, then loadInstrument("<plugin>.vst3") loads + activates the plug-in.
// render() outputs silence until a plug-in is loaded.
//
// All VST3 SDK types are hidden behind a pimpl so callers never include the SDK.
//
// Also implements IPluginEditor so the UI can host the plug-in's own editor
// window (P4-B) without touching the SDK.
class Vst3SynthEngine : public SynthesizerBase, public IPluginEditor {
 public:
  Vst3SynthEngine();
  ~Vst3SynthEngine() override;

  Vst3SynthEngine(const Vst3SynthEngine&) = delete;
  Vst3SynthEngine& operator=(const Vst3SynthEngine&) = delete;

  bool init(uint32_t sample_rate, uint32_t max_frames) override;
  void shutdown() override;

  // Loads a .vst3 instrument plug-in. Re-loadable: an already-loaded plug-in is
  // torn down first. Requires init() to have been called (needs sample rate /
  // block size). Returns false on any failure (the previous plug-in, if any, is
  // gone — the engine renders silence).
  bool loadInstrument(const std::string& path) override;

  void noteOn(uint8_t ch, uint8_t note, uint8_t vel) override;
  void noteOff(uint8_t ch, uint8_t note) override;
  void controlChange(uint8_t ch, uint8_t cc, uint8_t val) override;
  void allNotesOff() override;
  void render(float* out, uint32_t frames) override;

  // ── IPluginEditor ──────────────────────────────────────────────────────────
  bool hasEditor() const override;
  bool openEditor(void* native_parent, int& out_w, int& out_h) override;
  void closeEditor() override;
  void setEditorResizeCallback(std::function<void(int, int)> cb) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_VST3SYNTHENGINE_H_
