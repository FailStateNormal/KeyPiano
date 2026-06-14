#include "Vst3SynthEngine.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace keypiano {

// One shared host context for the whole process (PluginContextFactory is a
// singleton; keypiano only ever runs a single synth instance).
static HostApplication& hostApp() {
  static HostApplication app;
  return app;
}

// Minimal IComponentHandler. Some plug-ins refuse to create an editor view
// without one. We don't drive automation, so parameter edits are accepted and
// dropped; restartComponent is a no-op (we don't reconfigure on the fly).
// ImplementsNonDestroyable → addRef/release are no-ops, so it can live as a
// plain Impl member (lifetime tied to the engine, never delete-on-release).
struct HostComponentHandler
    : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>> {
  tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID, ParamValue) override {
    return kResultOk;
  }
  tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

// IPlugFrame: the plug-in calls resizeView() when its editor wants to change
// size. We forward the new size to the UI (which resizes the Qt host window)
// and then acknowledge by calling view->onSize().
struct HostPlugFrame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame>> {
  std::function<void(int, int)> resize_cb;

  tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
    if (!newSize) return kInvalidArgument;
    if (resize_cb) resize_cb(newSize->getWidth(), newSize->getHeight());
    if (view) view->onSize(newSize);
    return kResultTrue;
  }
};

// ── Impl ────────────────────────────────────────────────────────────────────

struct Vst3SynthEngine::Impl {
  // Format negotiated by init(); needed when a plug-in is (re)loaded.
  uint32_t sample_rate = 44100;
  uint32_t max_frames = 256;

  VST3::Hosting::Module::Ptr module;
  IPtr<IComponent>       component;
  IPtr<IEditController>  controller;
  IPtr<IAudioProcessor>  processor;

  HostProcessData process_data;
  EventList       events{512};   // preallocated; audio thread only clears/adds
  ProcessContext  ctx{};

  bool processing = false;

  // Tracks held notes so allNotesOff() can emit matching NoteOffs.
  bool active[16][128] = {};

  // ── Editor (P4-B) ───────────────────────────────────────────────────────────
  // True when `controller` is a separate class instance we created+initialized
  // (vs. a single-component plug-in where the component IS the controller).
  bool controller_separate = false;
  IPtr<IPlugView>      view;            // current editor view, if open
  HostComponentHandler component_handler;
  HostPlugFrame        plug_frame;      // holds the UI resize callback

  void clearActive() { std::memset(active, 0, sizeof(active)); }

  void closeEditor() {
    if (view) {
      view->setFrame(nullptr);
      view->removed();
      view = nullptr;
    }
  }

  void unload() {
    closeEditor();
    if (processor && processing) processor->setProcessing(false);

    // Tear down the component<->controller connection before terminating.
    if (component && controller) {
      FUnknownPtr<IConnectionPoint> compCP(component);
      FUnknownPtr<IConnectionPoint> ctrlCP(controller);
      if (compCP && ctrlCP) {
        compCP->disconnect(ctrlCP);
        ctrlCP->disconnect(compCP);
      }
    }
    if (component) component->setActive(false);
    if (controller) {
      controller->setComponentHandler(nullptr);
      if (controller_separate) controller->terminate();  // else terminated with component
    }
    if (component) component->terminate();

    processing = false;
    controller_separate = false;
    process_data.unprepare();
    // Release in reverse dependency order.
    processor = nullptr;
    controller = nullptr;
    component = nullptr;
    module.reset();
    clearActive();
  }

  // Creates and wires up the edit controller (for the plug-in editor GUI).
  // Best-effort: failure here leaves `controller` null (audio still works, just
  // no editor). Must run after the component is initialized.
  void setupController(VST3::Hosting::PluginFactory& factory) {
    TUID controllerCID;
    if (component->getControllerClassId(controllerCID) == kResultTrue) {
      controller =
          factory.createInstance<IEditController>(VST3::UID::fromTUID(controllerCID));
      controller_separate = (controller != nullptr);
    }
    if (!controller) {
      // Single-component plug-in: the component also implements IEditController.
      controller = FUnknownPtr<IEditController>(component);
      controller_separate = false;
    }
    if (!controller) return;

    if (controller_separate &&
        controller->initialize(&hostApp()) != kResultTrue) {
      controller = nullptr;
      return;
    }
    controller->setComponentHandler(&component_handler);

    // Connect component and controller so parameter/message traffic flows.
    FUnknownPtr<IConnectionPoint> compCP(component);
    FUnknownPtr<IConnectionPoint> ctrlCP(controller);
    if (compCP && ctrlCP) {
      compCP->connect(ctrlCP);
      ctrlCP->connect(compCP);
    }
    // NOTE: component->getState() → controller->setComponentState() would sync
    // the editor to the current DSP state, but Steinberg's MemoryStream isn't
    // compiled into the vcpkg libs. Deferred; editors open with defaults.
  }

  bool load(const std::string& path) {
    unload();

    std::string err;
    module = VST3::Hosting::Module::create(path, err);
    if (!module) return false;

    auto factory = module->getFactory();
    factory.setHostContext(&hostApp());

    // Pick the audio component; prefer one tagged as an instrument.
    VST3::Hosting::ClassInfo chosen;
    bool found = false;
    for (const auto& ci : factory.classInfos()) {
      if (ci.category() != kVstAudioEffectClass) continue;
      if (!found) { chosen = ci; found = true; }
      if (ci.subCategoriesString().find("Instrument") != std::string::npos) {
        chosen = ci;
        break;
      }
    }
    if (!found) { unload(); return false; }

    // Create + initialize the processing component directly (no PlugProvider —
    // the editor controller is created lazily in P4-B for the plug-in UI).
    component = factory.createInstance<IComponent>(chosen.ID());
    if (!component) { unload(); return false; }
    if (component->initialize(&hostApp()) != kResultTrue) { unload(); return false; }

    processor = FUnknownPtr<IAudioProcessor>(component);
    if (!processor) { unload(); return false; }

    if (processor->canProcessSampleSize(kSample32) != kResultTrue) {
      unload();
      return false;
    }

    // Ask for a stereo main output (instruments have no audio input bus).
    SpeakerArrangement out_arr = SpeakerArr::kStereo;
    processor->setBusArrangements(nullptr, 0, &out_arr, 1);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = static_cast<int32>(max_frames);
    setup.sampleRate = static_cast<SampleRate>(sample_rate);
    if (processor->setupProcessing(setup) != kResultTrue) { unload(); return false; }

    if (component->getBusCount(kAudio, kOutput) > 0)
      component->activateBus(kAudio, kOutput, 0, true);
    if (component->getBusCount(kEvent, kInput) > 0)
      component->activateBus(kEvent, kInput, 0, true);

    if (!process_data.prepare(*component, static_cast<int32>(max_frames),
                              kSample32)) {
      unload();
      return false;
    }
    process_data.processMode = kRealtime;
    process_data.symbolicSampleSize = kSample32;
    process_data.inputEvents = &events;
    process_data.processContext = &ctx;

    ctx = {};
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
                ProcessContext::kTimeSigValid;
    ctx.sampleRate = static_cast<double>(sample_rate);
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;

    // Best-effort: editor GUI support. Failure leaves audio fully functional.
    setupController(factory);

    component->setActive(true);
    processor->setProcessing(true);
    processing = true;
    return true;
  }
};

// ── SynthesizerBase implementation ──────────────────────────────────────────

Vst3SynthEngine::Vst3SynthEngine() : impl_(std::make_unique<Impl>()) {}

Vst3SynthEngine::~Vst3SynthEngine() {
  if (impl_) impl_->unload();
}

bool Vst3SynthEngine::init(uint32_t sample_rate, uint32_t max_frames) {
  impl_->sample_rate = sample_rate;
  impl_->max_frames = max_frames;
  // No plug-in loaded yet — render() outputs silence until loadInstrument().
  return true;
}

void Vst3SynthEngine::shutdown() { impl_->unload(); }

bool Vst3SynthEngine::loadInstrument(const std::string& path) {
  return impl_->load(path);
}

void Vst3SynthEngine::noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  if (!impl_->processing || note >= 128) return;
  Event e{};
  e.busIndex = 0;
  e.sampleOffset = 0;
  e.ppqPosition = 0.0;
  e.flags = Event::kIsLive;
  e.type = Event::kNoteOnEvent;
  e.noteOn.channel = static_cast<int16>(ch & 0x0F);
  e.noteOn.pitch = static_cast<int16>(note);
  e.noteOn.tuning = 0.0f;
  e.noteOn.velocity = static_cast<float>(vel) / 127.0f;
  e.noteOn.length = 0;
  e.noteOn.noteId = -1;
  impl_->events.addEvent(e);
  impl_->active[ch & 0x0F][note] = true;
}

void Vst3SynthEngine::noteOff(uint8_t ch, uint8_t note) {
  if (!impl_->processing || note >= 128) return;
  Event e{};
  e.busIndex = 0;
  e.sampleOffset = 0;
  e.ppqPosition = 0.0;
  e.flags = Event::kIsLive;
  e.type = Event::kNoteOffEvent;
  e.noteOff.channel = static_cast<int16>(ch & 0x0F);
  e.noteOff.pitch = static_cast<int16>(note);
  e.noteOff.velocity = 0.0f;
  e.noteOff.noteId = -1;
  e.noteOff.tuning = 0.0f;
  impl_->events.addEvent(e);
  impl_->active[ch & 0x0F][note] = false;
}

void Vst3SynthEngine::controlChange(uint8_t /*ch*/, uint8_t /*cc*/,
                                    uint8_t /*val*/) {
  // VST3 routes MIDI CC through IMidiMapping + parameter changes rather than
  // raw CC events. Deferred to a later P4 iteration (sustain/expression).
}

void Vst3SynthEngine::allNotesOff() {
  if (!impl_->processing) return;
  for (int ch = 0; ch < 16; ++ch) {
    for (int note = 0; note < 128; ++note) {
      if (!impl_->active[ch][note]) continue;
      Event e{};
      e.busIndex = 0;
      e.sampleOffset = 0;
      e.ppqPosition = 0.0;
      e.flags = Event::kIsLive;
      e.type = Event::kNoteOffEvent;
      e.noteOff.channel = static_cast<int16>(ch);
      e.noteOff.pitch = static_cast<int16>(note);
      e.noteOff.velocity = 0.0f;
      e.noteOff.noteId = -1;
      e.noteOff.tuning = 0.0f;
      impl_->events.addEvent(e);
      impl_->active[ch][note] = false;
    }
  }
}

void Vst3SynthEngine::render(float* out, uint32_t frames) {
  if (!impl_->processing || !impl_->processor) {
    impl_->events.clear();
    return;
  }
  frames = std::min(frames, impl_->max_frames);

  auto& pd = impl_->process_data;
  pd.numSamples = static_cast<int32>(frames);
  impl_->processor->process(pd);

  // Mix the main output bus (bus 0) additively into the interleaved buffer.
  if (pd.numOutputs > 0 && pd.outputs && pd.outputs[0].numChannels >= 1 &&
      pd.outputs[0].channelBuffers32) {
    const auto& bus = pd.outputs[0];
    const float* L = bus.channelBuffers32[0];
    const float* R = bus.numChannels > 1 ? bus.channelBuffers32[1] : L;
    if (L && R) {
      for (uint32_t i = 0; i < frames; ++i) {
        out[i * 2]     += L[i];
        out[i * 2 + 1] += R[i];
      }
    }
  }

  impl_->events.clear();
}

// ── IPluginEditor implementation (P4-B) ─────────────────────────────────────

bool Vst3SynthEngine::hasEditor() const {
  return impl_->controller != nullptr;
}

bool Vst3SynthEngine::openEditor(void* native_parent, int& out_w, int& out_h) {
  out_w = 0;
  out_h = 0;
  if (!impl_->controller || !native_parent) return false;

  impl_->closeEditor();  // ensure no stale view

  IPtr<IPlugView> v = owned(impl_->controller->createView(ViewType::kEditor));
  if (!v) return false;  // plug-in has no custom editor
  if (v->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) return false;

  v->setFrame(&impl_->plug_frame);

  ViewRect rect{};
  if (v->getSize(&rect) == kResultTrue) {
    out_w = rect.getWidth();
    out_h = rect.getHeight();
  }

  if (v->attached(native_parent, kPlatformTypeHWND) != kResultTrue) {
    v->setFrame(nullptr);
    return false;
  }
  impl_->view = v;
  return true;
}

void Vst3SynthEngine::closeEditor() { impl_->closeEditor(); }

void Vst3SynthEngine::setEditorResizeCallback(std::function<void(int, int)> cb) {
  impl_->plug_frame.resize_cb = std::move(cb);
}

}  // namespace keypiano
