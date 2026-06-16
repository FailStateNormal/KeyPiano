#ifndef KEYPIANO_UI_SYNTHCONTROLLER_H_
#define KEYPIANO_UI_SYNTHCONTROLLER_H_

#include <functional>
#include <memory>

#include <QObject>
#include <QString>

#include "audio/AudioEngine.h"
#include "synth/SynthesizerBase.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace keypiano::ui {

class AudioBridge;
class Vst3EditorWindow;

// Owns the synth backend, the audio engine, the audio bridge and the active
// instrument state, plus everything about (re)starting the engine, switching
// backends transactionally (with rollback) and loading SF2 / VST3 instruments.
// Extracted from MainWindow so the window stays a thin assembler.
//
// Threading & lifecycle: lives on the UI thread. The engine teardown/bring-up
// path is the only thread-sensitive part — the audio callback (RtAudio) and the
// keyboard-hook thread both reach into engine_/synth_. To keep the original,
// carefully-ordered safety invariants intact after the extraction, the host
// (MainWindow) supplies two synchronous callbacks the controller invokes at the
// exact points the old stopEngine()/startEngine() did:
//
//   Host::beforeEngineStop — called FIRST on every teardown, before the engine
//       and bridge are stopped. It must uninstall the keyboard hook (joining its
//       thread) and release the recorder, in that order, so no hook callback or
//       playback dispatch touches engine_/synth_ while they are being replaced.
//   Host::afterEngineStart — called after a (re)start. `engine` is null if the
//       engine failed to open; `bridge` is the live AudioBridge (null when the
//       engine is null). It must rebuild the recorder for `engine`, install the
//       hook (only if engine is non-null), reset pedal state, and reconnect the
//       bridge→piano signals — in that order (recorder before hook so the hook
//       thread never feeds a stale recorder).
//
// These mirror the exact sequencing of the pre-extraction code; the controller
// only relocates *where* the synth/engine live, not *when* each step runs.
class SynthController : public QObject {
    Q_OBJECT

public:
    using Config = audio::AudioEngine::Config;

    // Which synth backend is currently active (drives Open SF2 / Open VST3).
    enum class Backend { FluidSynth, Vst3 };

    // Cross-cutting steps the host must sequence around an engine teardown/start.
    // See the class comment for the required ordering.
    struct Host {
        std::function<void()> beforeEngineStop;
        std::function<void(audio::AudioEngine*, AudioBridge*)> afterEngineStart;
        // Called whenever the active instrument changes (name / backend / built-in
        // flag). The host refreshes the status-bar label and the editor action.
        std::function<void()> onInstrumentChanged;
        // Parents the rollback message boxes.
        QWidget* dialogParent = nullptr;
    };

    explicit SynthController(QObject* parent = nullptr);
    ~SynthController() override;

    // One-time wiring of the host callbacks; call before initialize().
    void attach(Host host) { host_ = std::move(host); }

    // Build the FluidSynth backend and open the engine with `cfg`. Returns true if
    // the engine came up. On failure the host still gets afterEngineStart(nullptr,
    // nullptr) so it can install an inert recorder. Loads no instrument yet —
    // call loadDefault() after.
    bool initialize(const Config& cfg);

    // Tear the engine down for good (used on window close). Runs beforeEngineStop
    // then stops the bridge and closes the engine. The plug-in editor must already
    // be closed by the caller (closePluginEditor()).
    void shutdown();

    // Reopen the engine with a new config (settings change): stop + start, same
    // backend and synth_. Used by the audio Settings dialog.
    void restart(const Config& cfg);

    // ── Instrument loading (returns success; handles rollback + status) ─────────
    // Load an SF2 file. If currently on VST3, rebuilds the FluidSynth backend and
    // rolls back to the previous instrument if the load fails; if already on
    // FluidSynth, loads on the live synth (a failed load keeps the current sound).
    bool loadSf2(const QString& path, QString* err);
    // Load a VST3 bundle folder. Rebuilds the VST3 backend; rolls back to the
    // previous working instrument if the bundle can't be loaded.
    bool loadVst3(const QString& path, QString* err);
    // Load the bundled GeneralUser GS piano (or the embedded fallback). Used at
    // startup and as the last-resort rollback target.
    void loadDefault();

    // Absolute path to the bundled GeneralUser GS piano next to the exe, or empty
    // if it isn't there. Shared with the Open SF2 dialog (pins it as a default).
    QString bundledDefaultSf2Path() const;

    // ── Plug-in editor ──────────────────────────────────────────────────────────
    bool hasPluginEditor() const;     // current backend exposes a custom editor
    void showPluginEditor(QWidget* parent);  // open/raise the editor window
    void closePluginEditor();         // detach + close (before synth_ is replaced)

    // ── Accessors for the host (status bar, key handler, settings) ──────────────
    audio::AudioEngine* engine() const { return engine_.get(); }
    Backend  currentBackend() const { return current_backend_; }
    QString  currentName()    const { return current_name_; }
    QString  currentSf2Path() const { return current_sf2_path_; }
    bool     isBuiltin()      const { return builtin_; }
    const Config& config()    const { return cfg_; }

signals:
    // Show a status-bar message (timeout_ms == 0 → persistent). Mirrors the
    // KeymapController/RecordingController convention.
    void status(const QString& msg, int timeout_ms);

private:
    // A snapshot of the active instrument, enough to rebuild it if a backend
    // switch fails. `path` is the SF2 file / VST3 bundle (empty => built-in).
    struct InstrumentState {
        Backend backend = Backend::FluidSynth;
        QString path;
        QString name;
        bool    builtin = false;
    };
    InstrumentState snapshot() const;
    void restore(const InstrumentState& s);

    // Stop the engine (runs beforeEngineStop, then stops bridge + closes engine).
    void stopEngine();
    // Open the engine with `cfg` against the current synth_, then runs
    // afterEngineStart. Sets cfg_ on success.
    void startEngine(const Config& cfg);
    // Tear down + rebuild on a freshly-created `target` backend (no instrument).
    // Returns true if the engine came back up with a synth.
    bool rebuildBackend(Backend target);

    void notifyInstrumentChanged();  // host_.onInstrumentChanged() if set

    Host host_;

    std::unique_ptr<SynthesizerBase>    synth_;
    std::unique_ptr<audio::AudioEngine> engine_;
    std::unique_ptr<AudioBridge>        audio_bridge_;
    Vst3EditorWindow*                   editor_window_ = nullptr;  // nulled on destroy

    Config  cfg_;  // last-applied engine config
    Backend current_backend_ = Backend::FluidSynth;
    QString current_name_ = QStringLiteral("(none)");
    QString current_sf2_path_;   // full path of a user SF2 (empty for built-in)
    QString current_vst3_path_;  // full path of the active VST3 bundle
    bool    builtin_ = false;    // current instrument is the bundled default
};

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_SYNTHCONTROLLER_H_
