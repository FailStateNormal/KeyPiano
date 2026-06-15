#ifndef KEYPIANO_CORE_PLATFORM_WIN32_WIN32KEYBOARDHOOK_H_
#define KEYPIANO_CORE_PLATFORM_WIN32_WIN32KEYBOARDHOOK_H_

#include "platform/KeyboardHook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <functional>
#include <future>
#include <thread>

namespace keypiano {

class Win32KeyboardHook final : public KeyboardHook {
 public:
  Win32KeyboardHook();
  ~Win32KeyboardHook() override;

  Win32KeyboardHook(const Win32KeyboardHook&) = delete;
  Win32KeyboardHook& operator=(const Win32KeyboardHook&) = delete;

  bool install(std::function<void(const KeyEvent&)> cb) override;
  void uninstall() override;

 private:
  // WH_KEYBOARD_LL low-level hook procedure (called on hook_thread_).
  static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

  // Entry point for the dedicated hook thread.
  void RunMessageLoop(std::promise<bool>& ready);

  std::thread              hook_thread_;
  std::atomic<DWORD>       thread_id_{0};
  HHOOK                    hook_{nullptr};
  std::function<void(const KeyEvent&)> callback_;
  std::atomic<bool>        installed_{false};

  // Tracks which virtual keys are currently held, so auto-repeat WM_KEYDOWN
  // messages (Windows resends keydown ~30x/s while a key is held) are dropped:
  // a key already marked down that sends another keydown is a repeat, not a new
  // press. Without this, a held key retriggers noteOn continuously, restarting
  // the sample over and over (audible as a wobble). Touched only on the hook
  // thread inside HookProc, so no synchronization is needed.
  bool key_down_[256] = {};

  // Singleton pointer so the static HookProc can reach the instance.
  // Only one Win32KeyboardHook may be installed at a time.
  static Win32KeyboardHook* s_instance_;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_PLATFORM_WIN32_WIN32KEYBOARDHOOK_H_
