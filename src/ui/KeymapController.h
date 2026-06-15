#ifndef KEYPIANO_UI_KEYMAPCONTROLLER_H_
#define KEYPIANO_UI_KEYMAPCONTROLLER_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include <QObject>
#include <QString>

#include "keymap/KeyMap.h"

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QWidget;
QT_END_NAMESPACE

namespace keypiano::ui {

class PianoWidget;
class KeyboardOverlayWidget;

// Owns everything about the active key map: the editable working copy, the
// immutable snapshot the keyboard-hook thread reads, persistence (user.map +
// presets), and the click-to-rebind flow. Extracted from MainWindow so the
// window stays a thin assembler. Lives on the UI thread; the only members the
// hook thread touches are the two atomics, accessed via snapshot() /
// tryCaptureRebind().
class KeymapController : public QObject {
    Q_OBJECT

public:
    // `dialog_parent` is the window used to parent message boxes / file dialogs.
    explicit KeymapController(QWidget* dialog_parent, QObject* parent = nullptr);

    // One-time wiring once the widgets / menu actions exist.
    void setWidgets(PianoWidget* piano, KeyboardOverlayWidget* overlay);
    void setActions(QAction* edit_labels, QAction* rebind, QAction* clear,
                    QMenu* preset_menu);

    // Load the user's saved user.map, falling back to the bundled default.
    // Call after setWidgets()/setActions().
    void loadStartup();

    // ── Hook-thread API (thread-safe) ───────────────────────────────────────
    // Current immutable key map. Returns null until the first load completes.
    std::shared_ptr<const KeyMap> snapshot() const {
        return keymap_ptr_.load(std::memory_order_acquire);
    }
    // If a piano key is armed for rebinding, capture `vk_code` (marshalled to the
    // UI thread) and return true; otherwise return false. Caller should only pass
    // key-down events.
    bool tryCaptureRebind(uint32_t vk_code);
    // In clear mode every mapped key press is consumed and its binding removed.
    // Returns true (event swallowed) while clear mode is active. Key-down only.
    bool tryCaptureClear(uint32_t vk_code);

    // Repopulate the Presets submenu from disk. Also re-run on a language change
    // so the dynamic entries pick up the new translation.
    void rebuildPresetMenu();

public slots:
    void openKeymap();            // File ▸ Open Keymap…
    void editLabels();            // File ▸ Edit Keymap Labels…
    void toggleRebind(bool on);   // File ▸ Rebind Keys
    void toggleClear(bool on);    // File ▸ Clear Key Binding
    void resetToDefault();        // File ▸ Reset Keymap to Default
    void onRebindKeyClicked(int midi_note);  // PianoWidget click in rebind mode
    void savePreset();
    void deletePreset();
    void loadPreset(const QString& name);

signals:
    // Show a status-bar message (empty text = clear). MainWindow forwards it.
    void status(const QString& msg, int timeout_ms);

private:
    void loadDefault();             // load embedded :/keymaps/default.map
    void setActiveKeymap(KeyMap km);// adopt km: publish snapshot + overlay + enable
    void publishKeymap();           // store a fresh immutable snapshot from keymap_
    void applyRebind(uint32_t vk_code);  // UI thread (marshalled from the hook)
    void applyClear(uint32_t vk_code);   // UI thread (marshalled from the hook)
    void saveUserKeymap();
    QString userKeymapPath() const;  // %APPDATA%/keypiano/user.map
    QString presetsDir() const;      // %APPDATA%/keypiano/presets

    QWidget*               dialog_parent_ = nullptr;
    PianoWidget*           piano_   = nullptr;
    KeyboardOverlayWidget* overlay_ = nullptr;
    QAction*               act_edit_   = nullptr;
    QAction*               act_rebind_ = nullptr;
    QAction*               act_clear_  = nullptr;
    QMenu*                 preset_menu_ = nullptr;

    // UI-thread working copy, edited in place by load/edit/rebind/preset actions.
    KeyMap keymap_;
    // Immutable snapshot the hook thread reads (published after every edit).
    std::atomic<std::shared_ptr<const KeyMap>> keymap_ptr_;

    // Rebind state. rebind_armed_ is read on the hook thread (atomic); the others
    // only on the UI thread.
    bool              rebind_mode_ = false;  // "Rebind Keys" toggle active
    int               rebind_note_ = -1;     // piano key chosen, awaiting a key
    std::atomic<bool> rebind_armed_{false};  // a piano key is selected
    // Clear-binding mode: while on, the hook thread swallows every mapped key and
    // its binding is removed. Read on the hook thread (atomic).
    std::atomic<bool> clear_mode_{false};
};

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_KEYMAPCONTROLLER_H_
