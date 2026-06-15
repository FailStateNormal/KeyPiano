#include "platform/win32/Win32KeyboardHook.h"

#include "platform/KeyboardHook.h"

#include <cassert>

namespace keypiano {

// ── Static member ─────────────────────────────────────────────────────────────

Win32KeyboardHook* Win32KeyboardHook::s_instance_ = nullptr;

// ── Construction / destruction ────────────────────────────────────────────────

Win32KeyboardHook::Win32KeyboardHook() = default;

Win32KeyboardHook::~Win32KeyboardHook() {
  uninstall();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool Win32KeyboardHook::install(std::function<void(const KeyEvent&)> cb) {
  if (installed_.load(std::memory_order_acquire)) return false;

  callback_ = std::move(cb);
  for (bool& k : key_down_) k = false;  // start with no keys held

  std::promise<bool> ready;
  std::future<bool>  result = ready.get_future();

  hook_thread_ = std::thread([this, &ready] { RunMessageLoop(ready); });

  bool ok = result.get();  // blocks until hook is installed (or failed)
  if (!ok) {
    hook_thread_.join();
    callback_ = nullptr;
  }
  return ok;
}

void Win32KeyboardHook::uninstall() {
  if (!installed_.load(std::memory_order_acquire)) return;

  DWORD tid = thread_id_.load(std::memory_order_acquire);
  if (tid != 0) {
    // Wake up the message loop so it can call UnhookWindowsHookEx and exit.
    PostThreadMessageW(tid, WM_QUIT, 0, 0);
  }

  if (hook_thread_.joinable()) hook_thread_.join();
}

// ── Hook thread internals ─────────────────────────────────────────────────────

void Win32KeyboardHook::RunMessageLoop(std::promise<bool>& ready) {
  thread_id_.store(GetCurrentThreadId(), std::memory_order_release);

  // WH_KEYBOARD_LL is a global hook; dwThreadId must be 0.
  s_instance_ = this;
  hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc,
                             GetModuleHandleW(nullptr), 0);

  bool ok = (hook_ != nullptr);
  installed_.store(ok, std::memory_order_release);
  ready.set_value(ok);  // unblocks install()

  if (!ok) {
    s_instance_ = nullptr;
    thread_id_.store(0, std::memory_order_release);
    return;
  }

  // Pump messages.  WH_KEYBOARD_LL callbacks are delivered here.
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  UnhookWindowsHookEx(hook_);
  hook_ = nullptr;
  s_instance_ = nullptr;
  installed_.store(false, std::memory_order_release);
  thread_id_.store(0, std::memory_order_release);
}

// ── Hook procedure (runs on hook_thread_) ─────────────────────────────────────

LRESULT CALLBACK Win32KeyboardHook::HookProc(int nCode,
                                              WPARAM wParam,
                                              LPARAM lParam) {
  if (nCode == HC_ACTION && s_instance_ && s_instance_->callback_) {
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

    KeyEvent ev;
    ev.vk_code    = kb->vkCode;
    ev.scan_code  = kb->scanCode;
    ev.is_keydown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    ev.is_extended = (kb->flags & LLKHF_EXTENDED) != 0;

    // Filter OS auto-repeat: only forward a keydown on the leading edge (the
    // key was not already held) and a keyup that matches a held key. This makes
    // one physical press = one noteOn / one noteOff, so a held key sustains the
    // note instead of retriggering it (which sounded like a wobble).
    bool forward = true;
    if (kb->vkCode < 256) {
      bool* held = &s_instance_->key_down_[kb->vkCode];
      if (ev.is_keydown) {
        forward = !*held;   // drop repeats
        *held = true;
      } else {
        forward = *held;    // drop stray keyup with no matching keydown
        *held = false;
      }
    }

    // Callback must return quickly — the hook times out after ~300 ms.
    if (forward) s_instance_->callback_(ev);
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<KeyboardHook> KeyboardHook::create() {
  return std::make_unique<Win32KeyboardHook>();
}

}  // namespace keypiano
