#ifndef KEYPIANO_UI_MAINWINDOW_H_
#define KEYPIANO_UI_MAINWINDOW_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include <QMainWindow>

#include "audio/AudioEngine.h"
#include "keymap/KeyMap.h"
#include "platform/KeyboardHook.h"

#include "I18n.h"

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QMenu;
class QShowEvent;
class QSlider;
class QTimer;
class QToolBar;
class QTranslator;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace keypiano::ui { class AudioBridge; }
namespace keypiano::ui { class PianoWidget; }
namespace keypiano::ui { class KeyboardOverlayWidget; }
namespace keypiano::ui { class PedalIndicatorWidget; }
namespace keypiano::ui { class KeymapController; }
namespace keypiano::ui { class RecordingController; }
namespace keypiano::ui { class SynthController; }

namespace keypiano::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    // Catches QEvent::LanguageChange (re-applies code-set strings) and
    // QEvent::ActivationChange (tracks focus for the background-play gate).
    void changeEvent(QEvent* event) override;
    // Seeds window_active_ on first show so the background-play gate is correct
    // before the first ActivationChange arrives.
    void showEvent(QShowEvent* event) override;

private slots:
    void onOpenSf2();
    void onOpenVst3();
    void onShowPluginEditor();
    void onOpenSettings();
    void onShowUsageGuide();
    void onShowAbout();
    void onExportWav();   // offline-render the current recording to a .wav file
    void updateStatus();

private:
    // How a pedal key behaves: Hold = engaged while held; Toggle = press toggles
    // engaged on/off (handier on a keyboard you press by hand).
    enum class PedalMode { Hold, Toggle };

    void setupMenus();
    void setupAudioMenu();   // builds the Audio menu (volume slider + background play)
    void setupHelpMenu();
    // Switch the UI language (installs/removes the embedded translator, persists
    // the choice, and refreshes the menu checkmarks).
    void setLanguage(Lang lang);
    void loadLanguageSetting();   // apply the language saved from a previous run
    void retranslateUi();         // re-set every string built in code
    // Switch pedal behaviour; releases any latched pedals and persists the choice.
    void setPedalMode(PedalMode mode);
    void loadPedalModeSetting();  // apply the pedal mode saved from a previous run

    // ── Master volume + background play ──────────────────────────────────────
    void onVolumeChanged(int percent);  // slider moved: label + apply + persist
    void applyMasterVolume();           // push the current slider value to the engine
    void setBackgroundPlay(bool on);    // toggle the gate + persist the choice
    void loadAudioPrefs();              // apply volume + background-play from last run
    // Silence every sounding note. Used when the window loses focus while
    // background play is off, so a note whose key-up we will now ignore can't hang.
    void panicAllNotes();
    void refreshSf2Label();       // rebuild the status-bar instrument label
    void setupToolBar();
    void setupStatusBar();
    void setupPianoWidget();
    // Runs on the keyboard-hook thread: routes one key event through the keymap
    // snapshot (owned by keymap_ctl_) to the engine / recorder / record-toggle.
    // No UI-thread state is touched directly (rebind capture and record toggles
    // are marshalled back to the UI thread).
    void handleKeyboardEvent(const KeyEvent& kev);

    // Rebuilds the status-bar instrument label from SynthController's state. Kept
    // in MainWindow because it owns the label + the recorder title.
    // (declared below near the status-bar members)

    // Enables/disables the "Show Plugin Editor" action based on the backend.
    void updateEditorAction();

    // Installs the keyboard hook (routes keys to handleKeyboardEvent). Reused by
    // SynthController's afterEngineStart callback on every engine (re)start.
    void installHook();

    Ui::MainWindow* ui_ = nullptr;

    // The keyboard hook is owned here because handleKeyboardEvent() (its callback)
    // lives in MainWindow. SynthController owns the synth/engine/bridge; it calls
    // back into MainWindow (beforeEngineStop/afterEngineStart) to uninstall/install
    // this hook at the exact points required for thread safety.
    std::unique_ptr<KeyboardHook> hook_;

    // Owns the synth backend, audio engine, bridge and instrument state. Created in
    // the constructor; the host callbacks are wired via attach() once the widgets
    // it coordinates (piano/pedal) and the recorder controller exist.
    SynthController* synth_ctl_ = nullptr;

    PianoWidget*             piano_widget_  = nullptr;
    KeyboardOverlayWidget*   overlay_       = nullptr;
    PedalIndicatorWidget*    pedal_widget_  = nullptr;

    // Owns the active key map (working copy + immutable hook-thread snapshot),
    // persistence (user.map/presets), and the click-to-rebind flow.
    KeymapController* keymap_ctl_ = nullptr;

    // Owns the Recorder + recording/playback flow and the Record action states.
    // handleKeyboardEvent() feeds it live events from the hook thread.
    RecordingController* rec_ctl_ = nullptr;

    // Only ever touched on the hook thread (octave/velocity/key-signature
    // actions mutate them; resolve reads them).
    ChannelState ch0_{};
    ChannelState ch1_{};

    // Pedal behaviour mode (enum declared at the top of the private section).
    std::atomic<PedalMode> pedal_mode_{PedalMode::Hold};  // UI writes, hook reads
    // Engaged state per pedal, indexed sustain=0 / sostenuto=1 / soft=2. Written
    // on the hook thread; the soft flag is also read there to scale note velocity.
    std::atomic<bool> pedal_engaged_[3];

    // Background play: when false, handleKeyboardEvent ignores keys unless our
    // window is active. window_active_ is written on the UI thread (showEvent /
    // ActivationChange) and read on the hook thread — both plain atomics.
    std::atomic<bool> background_play_{false};  // UI writes, hook reads
    std::atomic<bool> window_active_{false};    // UI writes, hook reads

    // Index 0/1/2 ↔ CC 64/66/67. Returns -1 if cc is not a pedal.
    static int pedalIndex(int cc) {
        return cc == 64 ? 0 : cc == 66 ? 1 : cc == 67 ? 2 : -1;
    }

    // Apply the soft pedal to a note velocity: when the soft pedal is engaged,
    // scale `vel` down (una corda). Used by both the keyboard and mouse paths so
    // clicking the on-screen keys is softened too. Returns vel unchanged if soft
    // is not engaged.
    uint8_t softVelocity(int vel) const;

    QMenu*   file_menu_       = nullptr;
    QMenu*   audio_menu_      = nullptr;
    QMenu*   rec_menu_        = nullptr;
    QMenu*   help_menu_       = nullptr;
    QMenu*   lang_menu_       = nullptr;
    QToolBar* toolbar_        = nullptr;
    QAction* act_background_play_ = nullptr;
    QSlider* volume_slider_   = nullptr;
    QLabel*  lbl_volume_      = nullptr;  // "Volume" caption inside the Audio menu
    QLabel*  lbl_volume_pct_  = nullptr;  // live "85%" readout next to the slider
    QAction* act_open_sf2_     = nullptr;
    QAction* act_open_vst3_    = nullptr;
    QAction* act_show_editor_  = nullptr;
    QAction* act_open_keymap_  = nullptr;
    QAction* act_rebind_      = nullptr;
    QAction* act_clear_       = nullptr;
    QAction* act_reset_keymap_ = nullptr;
    QMenu*   preset_menu_     = nullptr;  // dynamic list of saved keymap presets
    QMenu*   pedal_mode_menu_ = nullptr;
    QAction* act_pedal_hold_   = nullptr;
    QAction* act_pedal_toggle_ = nullptr;
    QAction* act_settings_    = nullptr;
    QAction* act_exit_        = nullptr;
    QAction* act_rec_start_   = nullptr;
    QAction* act_stop_        = nullptr;
    QAction* act_playback_    = nullptr;
    QAction* act_open_rec_    = nullptr;
    QAction* act_export_wav_  = nullptr;
    QAction* act_usage_guide_ = nullptr;
    QAction* act_about_       = nullptr;
    QAction* act_lang_en_     = nullptr;
    QAction* act_lang_zh_     = nullptr;

    Lang        lang_       = Lang::English;
    Translator* translator_ = nullptr;  // installed only in Chinese mode

    QLabel* lbl_latency_  = nullptr;
    QLabel* lbl_cpu_      = nullptr;
    QLabel* lbl_sf2_name_ = nullptr;
    // Dropped-event indicator: hidden unless the engine has dropped input or
    // feedback events (queue full), in which case it shows the counts in red.
    QLabel* lbl_drops_    = nullptr;

    QTimer*  status_timer_     = nullptr;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_MAINWINDOW_H_
