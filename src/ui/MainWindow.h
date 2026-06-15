#ifndef KEYPIANO_UI_MAINWINDOW_H_
#define KEYPIANO_UI_MAINWINDOW_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include <QMainWindow>

#include "audio/AudioEngine.h"
#include "keymap/KeyMap.h"
#include "platform/KeyboardHook.h"
#include "recorder/Recorder.h"
#include "synth/SynthesizerBase.h"

QT_BEGIN_NAMESPACE
class QAction;
class QCloseEvent;
class QLabel;
class QMenu;
class QTimer;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace keypiano::ui { class AudioBridge; }
namespace keypiano::ui { class PianoWidget; }
namespace keypiano::ui { class KeyboardOverlayWidget; }
namespace keypiano::ui { class Vst3EditorWindow; }

namespace keypiano::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenSf2();
    void onOpenVst3();
    void onShowPluginEditor();
    void onOpenKeymap();
    void onEditKeymap();
    void onToggleRebind(bool on);
    void onResetKeymap();
    void onRebindKeyClicked(int midi_note);
    void onSavePreset();
    void onDeletePreset();
    void onLoadPreset(const QString& name);
    void onOpenSettings();
    void onStartRecording();
    void onStop();
    void onStartPlayback();
    void updateStatus();

private:
    void setupMenus();
    void setupToolBar();
    void setupStatusBar();
    void setupEngine();
    void setupPianoWidget();
    void loadStartupKeymap();   // prefer the user's saved user.map, else the default
    void loadDefaultKeymap();   // load embedded :/keymaps/default.map
    QString userKeymapPath() const;  // %APPDATA%/keypiano/user.map
    QString presetsDir() const;      // %APPDATA%/keypiano/presets
    void rebuildPresetMenu();        // repopulate the Presets submenu from disk
    void saveUserKeymap();           // serialize keymap_ to user.map (rebind persistence)
    // Applies a captured physical key to the note selected for rebinding. Called
    // on the UI thread (marshalled from the hook thread via a queued invoke).
    void applyRebind(uint32_t vk_code);
    void loadDefaultSoundFont();// load embedded piano SF2 so the app is audible out of the box
    void syncRecordActions();

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
    //   synth_ → engine_ → hook_ → recorder_ → audio_bridge_
    // Destruction runs in reverse, so audio_bridge_ (timer) stops before engine_.
    std::unique_ptr<SynthesizerBase>    synth_;
    std::unique_ptr<audio::AudioEngine> engine_;
    std::unique_ptr<KeyboardHook>       hook_;
    std::unique_ptr<Recorder>           recorder_;
    std::unique_ptr<AudioBridge>        audio_bridge_;

    PianoWidget*             piano_widget_  = nullptr;
    KeyboardOverlayWidget*   overlay_       = nullptr;
    Vst3EditorWindow*        editor_window_ = nullptr;  // nulled on destroy

    KeyMap       keymap_;
    ChannelState ch0_{};
    ChannelState ch1_{};

    // Rebind state. rebind_armed_ is read on the hook thread (atomic); rebind_note_
    // and rebind_mode_ are touched only on the UI thread.
    bool              rebind_mode_  = false;  // "Rebind Keys" toggle active
    int               rebind_note_  = -1;     // piano key chosen, awaiting a phys key
    std::atomic<bool> rebind_armed_{false};   // a piano key is selected; capture next key

    // Set just before startRecording() so the hook thread can compute ts_us
    // for each captured event (happens-before guaranteed via atomic state_).
    std::chrono::steady_clock::time_point record_start_{};

    // Which synth backend is currently active (drives Open SF2 / Open VST3).
    enum class Backend { FluidSynth, Vst3 };
    Backend current_backend_ = Backend::FluidSynth;

    QAction* act_open_sf2_     = nullptr;
    QAction* act_open_vst3_    = nullptr;
    QAction* act_show_editor_  = nullptr;
    QAction* act_open_keymap_  = nullptr;
    QAction* act_edit_keymap_ = nullptr;
    QAction* act_rebind_      = nullptr;
    QAction* act_reset_keymap_ = nullptr;
    QMenu*   preset_menu_     = nullptr;  // dynamic list of saved keymap presets
    QAction* act_settings_    = nullptr;
    QAction* act_rec_start_   = nullptr;
    QAction* act_stop_        = nullptr;
    QAction* act_playback_    = nullptr;

    audio::AudioEngine::Config audio_cfg_;  // last-applied engine config

    QLabel* lbl_latency_  = nullptr;
    QLabel* lbl_cpu_      = nullptr;
    QLabel* lbl_sf2_name_ = nullptr;

    QTimer*  status_timer_     = nullptr;
    QString  current_sf2_name_ = "(none)";
    // Full path of the user-loaded SF2 (empty for the built-in default). Used to
    // pre-select the right entry when reopening the dialog — must be a real path,
    // not the display name, or it pollutes the recent list with an invalid entry.
    QString  current_sf2_path_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_MAINWINDOW_H_
