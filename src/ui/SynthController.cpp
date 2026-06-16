#include "SynthController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

#include "bridge/AudioBridge.h"
#include "widgets/Vst3EditorWindow.h"
#include "synth/IPluginEditor.h"
#include "synth/InstrumentRestore.h"
#include "synth/SynthFactory.h"

namespace keypiano::ui {

SynthController::SynthController(QObject* parent) : QObject(parent) {}

SynthController::~SynthController() {
    // Safety net only — the orderly shutdown (with host callbacks) runs in
    // shutdown() during closeEvent. Do NOT call host callbacks here: the host may
    // already be partly torn down. Just stop the bridge timer and the audio
    // callback before the synth is released (unique_ptr order: bridge, engine,
    // synth — matching the safe teardown direction).
    if (audio_bridge_) audio_bridge_->stop();
    if (engine_)       engine_->close();
}

// ── Engine lifecycle ─────────────────────────────────────────────────────────

bool SynthController::initialize(const Config& cfg) {
    synth_ = createFluidSynth();
    startEngine(cfg);
    return engine_ != nullptr;
}

void SynthController::shutdown() {
    stopEngine();
}

void SynthController::restart(const Config& cfg) {
    stopEngine();
    startEngine(cfg);
}

void SynthController::stopEngine() {
    // beforeEngineStop must run FIRST: it uninstalls the keyboard hook (joining
    // its thread) and releases the recorder, so neither a hook callback nor a
    // playback dispatch touches engine_/synth_ while we tear them down.
    if (host_.beforeEngineStop) host_.beforeEngineStop();
    if (audio_bridge_) { audio_bridge_->stop(); audio_bridge_.reset(); }
    // engine_->close() blocks until the audio callback has stopped (and shuts down
    // the current synth_), so once it returns synth_ may be safely swapped.
    if (engine_) { engine_->close(); engine_.reset(); }
}

void SynthController::startEngine(const Config& cfg) {
    engine_ = std::make_unique<audio::AudioEngine>();
    if (!engine_->open(cfg, synth_.get())) {
        engine_.reset();
        // Let the host install an inert recorder so the rest of the UI stays sane.
        if (host_.afterEngineStart) host_.afterEngineStart(nullptr, nullptr);
        return;
    }

    cfg_ = cfg;
    audio_bridge_ = std::make_unique<AudioBridge>(engine_.get());
    audio_bridge_->start();
    if (host_.afterEngineStart)
        host_.afterEngineStart(engine_.get(), audio_bridge_.get());
}

bool SynthController::rebuildBackend(Backend target) {
    closePluginEditor();  // the editor (if any) references the synth we replace
    stopEngine();         // stop the callback BEFORE swapping synth_
    if (target == Backend::Vst3) {
#ifdef KEYPIANO_ENABLE_VST3
        synth_ = createVst3Synth();
#else
        synth_ = nullptr;
#endif
    } else {
        synth_ = createFluidSynth();
    }
    current_backend_ = target;
    startEngine(cfg_);
    return engine_ != nullptr && synth_ != nullptr;
}

// ── Instrument state snapshot / restore ──────────────────────────────────────

SynthController::InstrumentState SynthController::snapshot() const {
    InstrumentState s;
    s.backend = current_backend_;
    s.name    = current_name_;
    s.builtin = builtin_;
    s.path    = (current_backend_ == Backend::Vst3) ? current_vst3_path_
                                                    : current_sf2_path_;
    return s;
}

void SynthController::restore(const InstrumentState& s) {
    // The target decision is a pure, separately-tested policy (Qt-free, in core).
    const InstrumentSnapshot snap{
        s.backend == Backend::Vst3 ? SynthBackend::Vst3 : SynthBackend::FluidSynth,
        s.path.toStdString(), s.builtin};

    bool ok = false;
    switch (planInstrumentRestore(snap)) {
        case RestoreTarget::ReloadVst3:
            // rebuildBackend handles the !KEYPIANO_ENABLE_VST3 case (synth_ == null
            // → the loadInstrument guard fails → falls through to the default).
            ok = rebuildBackend(Backend::Vst3) && synth_ &&
                 synth_->loadInstrument(s.path.toStdString());
            if (ok) {
                current_vst3_path_ = s.path;
                current_name_      = s.name;
                current_sf2_path_.clear();
                builtin_           = false;
            }
            break;
        case RestoreTarget::ReloadFluidUser:
            ok = rebuildBackend(Backend::FluidSynth) && synth_ &&
                 synth_->loadInstrument(s.path.toStdString());
            if (ok) {
                current_sf2_path_  = s.path;
                current_name_      = s.name;
                current_vst3_path_.clear();
                builtin_           = false;
            }
            break;
        case RestoreTarget::FallbackDefault:
            break;  // handled by the fallback below
    }
    // FallbackDefault, or any reload that failed above: fall back to the bundled
    // default piano so the user always has a working instrument.
    if (!ok) {
        rebuildBackend(Backend::FluidSynth);
        current_vst3_path_.clear();
        loadDefault();  // sets name + builtin flag + notifies
    }
    notifyInstrumentChanged();
}

// ── Instrument loading ───────────────────────────────────────────────────────

bool SynthController::loadSf2(const QString& path, QString* err) {
    if (current_backend_ != Backend::FluidSynth) {
        // Coming from a VST3 plug-in: rebuild on FluidSynth, then load. If the
        // load fails, roll back to the previous backend rather than strand the
        // user on an empty FluidSynth.
        const InstrumentState prev = snapshot();
        if (!(rebuildBackend(Backend::FluidSynth) && synth_ &&
              synth_->loadInstrument(path.toStdString()))) {
            if (err) *err = tr("Failed to load SoundFont:\n%1").arg(path);
            restore(prev);
            return false;
        }
    } else {
        // Already on FluidSynth: load on the live synth. loadInstrument() loads the
        // new font before unloading the old, so a failed load leaves the current
        // instrument playing — no teardown, no audio gap.
        if (!engine_ || !synth_) {
            if (err) *err = tr("Audio engine is not running.");
            return false;
        }
        if (!synth_->loadInstrument(path.toStdString())) {
            if (err) *err = tr("Failed to load SoundFont:\n%1").arg(path);
            return false;
        }
    }

    // If the user picked the bundled default, keep our state consistent with
    // loadDefault(): mark it built-in and keep it out of the recents path.
    const QString bundled = bundledDefaultSf2Path();
    const bool is_builtin =
        !bundled.isEmpty() &&
        QFileInfo(path).canonicalFilePath() == QFileInfo(bundled).canonicalFilePath();

    current_name_     = QFileInfo(path).fileName();
    current_sf2_path_ = is_builtin ? QString() : path;
    current_vst3_path_.clear();
    builtin_ = is_builtin;
    notifyInstrumentChanged();
    emit status(tr("Loaded: %1").arg(current_name_), 3000);
    return true;
}

bool SynthController::loadVst3(const QString& path, QString* err) {
#ifdef KEYPIANO_ENABLE_VST3
    // Snapshot the current working instrument so a bad bundle can be rolled back.
    const InstrumentState prev = snapshot();

    if (rebuildBackend(Backend::Vst3) && synth_ &&
        synth_->loadInstrument(path.toStdString())) {
        current_vst3_path_ = path;
        current_name_      = QFileInfo(path).fileName();
        current_sf2_path_.clear();
        builtin_           = false;
        notifyInstrumentChanged();
        emit status(tr("Loaded VST3: %1").arg(current_name_), 3000);
        return true;
    }

    // Load failed — restore the previous, working backend so the user is never
    // left without sound.
    if (err)
        *err = tr("Failed to load VST3 instrument:\n%1\n\n"
                  "Make sure the folder is a valid .vst3 bundle containing an "
                  "instrument plug-in.").arg(path);
    restore(prev);
    return false;
#else
    Q_UNUSED(path);
    if (err) *err = QStringLiteral("VST3 support is not built in.");
    return false;
#endif
}

void SynthController::loadDefault() {
    // FluidSynth's sfload() needs a real file path. The preferred default sound is
    // the bundled GeneralUser GS piano — a real sampled piano shipped as a loose
    // file beside the exe (too large, ~30 MB, for the .qrc). If it is missing we
    // fall back to the tiny synthetic piano embedded in the .qrc so the app is
    // always audible out of the box.
    if (!synth_ || current_backend_ != Backend::FluidSynth) return;

    // 1) Preferred: the bundled GeneralUser GS sampled piano next to the exe.
    const QString bundled = bundledDefaultSf2Path();
    if (!bundled.isEmpty() && synth_->loadInstrument(bundled.toStdString())) {
        current_name_ = QStringLiteral("GeneralUser-GS.sf2");
        builtin_      = true;
        current_sf2_path_.clear();
        notifyInstrumentChanged();
        return;
    }

    // 2) Fallback: extract the embedded synthetic piano to a per-user cache file
    // and load it. Always re-extract so an old build's stale SF2 never shadows an
    // updated default sound.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString sf2_path = dir + "/default_piano.sf2";
    {
        QFile src(":/soundfonts/default_piano.sf2");
        if (!src.open(QIODevice::ReadOnly)) return;
        QFile dst(sf2_path);
        if (!dst.open(QIODevice::WriteOnly)) return;
        dst.write(src.readAll());
        dst.close();
    }

    if (synth_->loadInstrument(sf2_path.toStdString())) {
        current_name_ = QStringLiteral("default_piano.sf2");
        builtin_      = true;
        notifyInstrumentChanged();
    }
}

QString SynthController::bundledDefaultSf2Path() const {
    const QString p =
        QCoreApplication::applicationDirPath() + "/soundfonts/GeneralUser-GS.sf2";
    return QFileInfo::exists(p) ? p : QString();
}

// ── Plug-in editor ───────────────────────────────────────────────────────────

bool SynthController::hasPluginEditor() const {
    auto* editor = dynamic_cast<IPluginEditor*>(synth_.get());
    return editor && editor->hasEditor();
}

void SynthController::showPluginEditor(QWidget* parent) {
    if (editor_window_) {            // already open — bring it forward
        editor_window_->show();
        editor_window_->raise();
        editor_window_->activateWindow();
        return;
    }

    auto* editor = dynamic_cast<IPluginEditor*>(synth_.get());
    if (!editor || !editor->hasEditor()) {
        QMessageBox::information(parent, tr("Plugin Editor"),
                                 tr("The current backend has no plug-in editor."));
        return;
    }

    auto* win = new Vst3EditorWindow(editor, parent);
    if (!win->openEditor()) {
        delete win;
        QMessageBox::information(
            parent, tr("Plugin Editor"),
            tr("This plug-in does not provide a custom editor window."));
        return;
    }
    editor_window_ = win;
    connect(win, &QObject::destroyed, this,
            [this] { editor_window_ = nullptr; });
    win->show();
}

void SynthController::closePluginEditor() {
    if (editor_window_) {
        // close() runs the window's closeEvent synchronously (detaching the
        // plug-in view) before WA_DeleteOnClose schedules deletion.
        editor_window_->close();
        editor_window_ = nullptr;
    }
}

void SynthController::notifyInstrumentChanged() {
    if (host_.onInstrumentChanged) host_.onInstrumentChanged();
}

}  // namespace keypiano::ui
