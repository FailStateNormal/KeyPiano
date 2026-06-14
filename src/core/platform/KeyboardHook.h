#ifndef KEYPIANO_CORE_PLATFORM_KEYBOARDHOOK_H_
#define KEYPIANO_CORE_PLATFORM_KEYBOARDHOOK_H_

#include <cstdint>
#include <functional>
#include <memory>

namespace keypiano {

struct KeyEvent {
  uint32_t vk_code;    // Windows VK_* virtual-key code
  uint32_t scan_code;  // hardware scan code
  bool     is_keydown;
  bool     is_extended;  // extended key (numpad, arrows, etc.)
};

// Platform-agnostic keyboard hook interface.
//
// Threading contract:
//   install() / uninstall() must be called from the same thread (typically the
//   UI thread).  The callback `cb` is invoked from an internal hook thread —
//   it MUST return quickly (< 300 ms) and must not call install/uninstall.
class KeyboardHook {
 public:
  virtual ~KeyboardHook() = default;

  // Install the global low-level keyboard hook.  `cb` is called for every
  // key-down and key-up event while the hook is active.
  // Returns false if the hook could not be installed.
  virtual bool install(std::function<void(const KeyEvent&)> cb) = 0;

  // Remove the hook and stop the internal thread.  Safe to call even if
  // install() was never called or returned false.
  virtual void uninstall() = 0;

  // Factory — returns the platform-native implementation.
  static std::unique_ptr<KeyboardHook> create();
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_PLATFORM_KEYBOARDHOOK_H_
