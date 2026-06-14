#ifndef KEYPIANO_CORE_SYNTH_IPLUGINEDITOR_H_
#define KEYPIANO_CORE_SYNTH_IPLUGINEDITOR_H_

#include <functional>

namespace keypiano {

// Optional capability for synth backends that expose a native plug-in editor
// window (currently only Vst3SynthEngine). The UI layer probes for it with
// dynamic_cast<IPluginEditor*>(synth) and, when present, hosts the plug-in's
// own GUI inside a Qt window.
//
// Deliberately free of GUI-toolkit and VST3 SDK types: the parent window is an
// opaque native handle (HWND on Windows) so the core library never leaks the
// SDK headers and the UI never includes them.
class IPluginEditor {
 public:
  virtual ~IPluginEditor() = default;

  // True if a plug-in is loaded and exposes an "editor" view. Note that not all
  // instruments ship a custom editor; openEditor() is the authoritative check.
  virtual bool hasEditor() const = 0;

  // Creates the plug-in's editor view, attaches it to the given native parent
  // window (an HWND on Windows), and reports its preferred client size in
  // out_w/out_h (pixels). Returns false if the plug-in has no editor or attach
  // failed. Call closeEditor() before the parent window is destroyed.
  virtual bool openEditor(void* native_parent, int& out_w, int& out_h) = 0;

  // Detaches and releases the plug-in view. Safe to call when nothing is open.
  virtual void closeEditor() = 0;

  // Registers a callback invoked when the plug-in requests a new view size
  // (width, height in pixels). Invoked on the UI thread (during the plug-in's
  // own message handling). Pass {} to clear.
  virtual void setEditorResizeCallback(std::function<void(int, int)> cb) = 0;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_SYNTH_IPLUGINEDITOR_H_
