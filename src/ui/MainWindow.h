#ifndef KEYPIANO_UI_MAINWINDOW_H_
#define KEYPIANO_UI_MAINWINDOW_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include <QMainWindow>

#include "audio/AudioEngine.h"
#include "keymap/KeyMap.h"
#include "platform/KeyboardHook.h"
#include "synth/SynthesizerBase.h"

#include "I18n.h"

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QMenu;
class QTimer;
class QToolBar;
class QTranslator;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace keypiano::ui { class AudioBridge; }
namespace keypiano::ui { class PianoWidget; }
namespace keypiano::ui { class KeyboardOverlayWidget; }
namespace keypiano::ui { class PedalIndicatorWidget; }
namespace keypiano::ui { class Vst3EditorWindow; }
namespace keypiano::ui { class KeymapController; }
namespace keypiano::ui { class RecordingController; }

namespace keypiano::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    // Catches QEvent::LanguageChange (sent when a translator is installed/removed)
    // and re-applies every code-set string via retranslateUi().
    void changeEvent(QEvent* event) override;

private slots:
    void onOpenSf2();
    void onOpenVst3();
    void onShowPluginEditor();
    void onOpenSettings();
    void onShowUsageGuide();
    void onShowAbout();
    void updateStatus();

private:
    // How a pedal key behaves: Hold = engaged while held; Toggle = press toggles
    // engaged on/off (handier on a keyboard you press by hand).
    enum class PedalMode { Hold, Toggle };

    void setupMenus();
    void setupHelpMenu();
    // Switch the UI language (installs/removes the embedded translator, persists
    // the choice, and refreshes the menu checkmarks).
    void setLanguage(Lang lang);
    void loadLanguageSetting();   // apply the language saved from a previous run
    void retranslateUi();         // re-set every string built in code
    // Switch pedal behaviour; releases any latched pedals and persists the choice.
    void setPedalMode(PedalMode mode);
    void loadPedalModeSetting();  // apply the pedal mode saved from a previous run
    void refreshSf2Label();       // rebuild the status-bar instrument label
    void setupToolBar();
    void setupStatusBar();
    void setupEngine();
    void setupPianoWidget();
    // Runs on the keyboard-hook thread: routes one key event through the keymap
    // snapshot (owned by keymap_ctl_) to the engine / recorder / record-toggle.
    // No UI-thread state is touched directly (rebind capture and record toggles
    // are marshalled back to the UI thread).
    void handleKeyboardEvent(const KeyEvent& kev);
    void loadDefaultSoundFont();// load embedded piano SF2 so the app is audible out of the box

    // Closes + detaches the VST3 plug-in editor window if open. Must be called
    // before synth_ is replaced or destroyed (the editor view references it).
    void closePluginEditor();
    // Enables/disables the "Show Plugin Editor" action based on the backend.
    void updateEditorAction();

    // Extracts the hook installation (lambda) so it can be reused on restart.
    void installHook();

    // Tear down + reopen the engine with a new config (= stopEngine + startEngine).
    void restartEngine(const audio::AudioEngine::Config& cfg);

    // Stop the audio engine (and bridge/hook/recorder). After this returns the
    // audio callback is stopped, so synth_ may be safely replaced or destroyed.
    // Splitting stop/start lets a backend switch swap synth_ *between* them,
    // avoiding a use-after-free of the synth the callback is still touching.
    void stopEngine();
    void startEngine(const audio::AudioEngine::Config& cfg);

    Ui::MainWindow* ui_ = nullptr;

    // Declaration order controls construction sequence:
    //   synth_ → engine_ → hook_ → audio_bridge_
    // Destruction runs in reverse, so audio_bridge_ (timer) stops before engine_.
    // The Recorder lives in rec_ctl_; closeEvent()/stopEngine() release it (after
    // the hook is uninstalled) before engine_ is torn down.
    std::unique_ptr<SynthesizerBase>    synth_;
    std::unique_ptr<audio::AudioEngine> engine_;
    std::unique_ptr<KeyboardHook>       hook_;
    std::unique_ptr<AudioBridge>        audio_bridge_;

    PianoWidget*             piano_widget_  = nullptr;
    KeyboardOverlayWidget*   overlay_       = nullptr;
    PedalIndicatorWidget*    pedal_widget_  = nullptr;
    Vst3EditorWindow*        editor_window_ = nullptr;  // nulled on destroy

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

    // Index 0/1/2 ↔ CC 64/66/67. Returns -1 if cc is not a pedal.
    static int pedalIndex(int cc) {
        return cc == 64 ? 0 : cc == 66 ? 1 : cc == 67 ? 2 : -1;
    }

    // Apply the soft pedal to a note velocity: when the soft pedal is engaged,
    // scale `vel` down (una corda). Used by both the keyboard and mouse paths so
    // clicking the on-screen keys is softened too. Returns vel unchanged if soft
    // is not engaged.
    uint8_t softVelocity(int vel) const;

    // Which synth backend is currently active (drives Open SF2 / Open VST3).
    enum class Backend { FluidSynth, Vst3 };
    Backend current_backend_ = Backend::FluidSynth;

    // ── Transactional backend switching ──────────────────────────────────────
    // Tears down the running engine and brings it back up on a freshly-built
    // `target` backend (no instrument loaded yet). Returns true if the engine
    // came back up. Building the new backend BEFORE proving the instrument loads
    // is not possible (AudioEngine::open() re-init()s the synth, which would wipe
    // a pre-loaded FluidSynth font), so the Open SF2 / Open VST3 flows instead
    // attempt the switch and, on a load failure, call restoreInstrument() to put
    // the previous working backend back — the user is never left without sound.
    bool rebuildBackend(Backend target);

    // A snapshot of the active instrument, enough to rebuild it if a backend
    // switch fails. `path` is the SF2 file / VST3 bundle (empty => built-in).
    struct InstrumentState {
        Backend backend = Backend::FluidSynth;
        QString path;
        QString name;
        bool    builtin = false;
    };
    InstrumentState currentInstrument() const;         // capture the live state
    void restoreInstrument(const InstrumentState& s);  // rebuild + reload + relabel

    QMenu*   file_menu_       = nullptr;
    QMenu*   rec_menu_        = nullptr;
    QMenu*   help_menu_       = nullptr;
    QMenu*   lang_menu_       = nullptr;
    QToolBar* toolbar_        = nullptr;
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
    QAction* act_usage_guide_ = nullptr;
    QAction* act_about_       = nullptr;
    QAction* act_lang_en_     = nullptr;
    QAction* act_lang_zh_     = nullptr;

    Lang        lang_       = Lang::English;
    Translator* translator_ = nullptr;  // installed only in Chinese mode

    audio::AudioEngine::Config audio_cfg_;  // last-applied engine config

    QLabel* lbl_latency_  = nullptr;
    QLabel* lbl_cpu_      = nullptr;
    QLabel* lbl_sf2_name_ = nullptr;

    QTimer*  status_timer_     = nullptr;
    QString  current_sf2_name_ = "(none)";
    bool     sf2_builtin_      = false;  // current instrument is the embedded default
    // Full path of the user-loaded SF2 (empty for the built-in default). Used to
    // pre-select the right entry when reopening the dialog — must be a real path,
    // not the display name, or it pollutes the recent list with an invalid entry.
    QString  current_sf2_path_;
    // Full path of the active VST3 bundle (empty unless a VST3 backend is live).
    // Kept so a failed backend switch can roll back to the previous VST3 plug-in.
    QString  current_vst3_path_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_MAINWINDOW_H_
