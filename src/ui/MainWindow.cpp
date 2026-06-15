#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <fstream>
#include <sstream>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "HelpContent.h"
#include "I18n.h"
#include "KeymapController.h"
#include "bridge/AudioBridge.h"
#include "widgets/PianoWidget.h"
#include "widgets/KeyboardOverlayWidget.h"
#include "widgets/PedalIndicatorWidget.h"
#include "widgets/Vst3EditorWindow.h"
#include "dialogs/SoundFontDialog.h"
#include "dialogs/SettingsDialog.h"
#include "recorder/KpsFormat.h"
#include "synth/IPluginEditor.h"
#include "synth/SynthFactory.h"

namespace {

// QSettings key storing the folder the "Open VST3" dialog last used. Persisted
// in the registry (HKCU) so it survives restarts and re-installs.
constexpr auto kVst3DirKey = "vst3_dir";

// Where the "Open VST3 Instrument" dialog should start:
//   1. the user's last-used folder (if it still exists), else
//   2. the first standard Windows VST3 install location that exists.
// Users change the default simply by browsing elsewhere — the new folder is
// remembered. Power users can also pre-seed/override the registry value
// HKCU\Software\keypiano\keypiano\vst3_dir.
QString vst3StartDir() {
    QSettings settings("keypiano", "keypiano");
    const QString saved = settings.value(kVst3DirKey).toString();
    if (!saved.isEmpty() && QDir(saved).exists()) return saved;

    const QString candidates[] = {
        QStringLiteral("C:/Program Files/Common Files/VST3"),
        QDir::homePath() + QStringLiteral("/AppData/Local/Programs/Common/VST3"),
    };
    for (const QString& dir : candidates) {
        if (QDir(dir).exists()) return dir;
    }
    return {};  // no standard location — let Qt fall back to the cwd
}

}  // namespace

namespace keypiano::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow) {
    ui_->setupUi(this);
    setWindowIcon(QIcon(":/icons/app.png"));

    keymap_ctl_ = new KeymapController(this, this);
    connect(keymap_ctl_, &KeymapController::status, this,
            [this](const QString& msg, int timeout) {
                if (msg.isEmpty()) statusBar()->clearMessage();
                else               statusBar()->showMessage(msg, timeout);
            });

    setupMenus();
    setupToolBar();
    setupHelpMenu();
    setupStatusBar();
    setupEngine();
    setupPianoWidget();
    keymap_ctl_->loadStartup();
    loadDefaultSoundFont();
    loadLanguageSetting();  // re-apply the language chosen in a previous session

    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    status_timer_->start(500);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    status_timer_->stop();
    closePluginEditor();  // detach plug-in view while synth_ is still alive
    if (piano_widget_) piano_widget_->releaseAll();
    if (audio_bridge_) audio_bridge_->stop();
    if (recorder_) {
        recorder_->stopPlayback();
        recorder_->stopRecording();
    }
    if (hook_)   hook_->uninstall();
    if (engine_) engine_->close();
    QMainWindow::closeEvent(event);
}

// ── Setup ──────────────────────────────────────────────────────────────────────

void MainWindow::setupMenus() {
    file_menu_ = menuBar()->addMenu(tr("&File"));

    act_open_sf2_ = new QAction(tr("Open SF&2..."), this);
    act_open_sf2_->setShortcut(QKeySequence("Ctrl+O"));
    connect(act_open_sf2_, &QAction::triggered, this, &MainWindow::onOpenSf2);
    file_menu_->addAction(act_open_sf2_);

#ifdef KEYPIANO_ENABLE_VST3
    act_open_vst3_ = new QAction(tr("Open &VST3 Instrument..."), this);
    act_open_vst3_->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(act_open_vst3_, &QAction::triggered, this, &MainWindow::onOpenVst3);
    file_menu_->addAction(act_open_vst3_);

    act_show_editor_ = new QAction(tr("Show Plugin &Editor..."), this);
    act_show_editor_->setShortcut(QKeySequence("Ctrl+E"));
    act_show_editor_->setEnabled(false);  // enabled once a VST3 editor is loaded
    connect(act_show_editor_, &QAction::triggered, this, &MainWindow::onShowPluginEditor);
    file_menu_->addAction(act_show_editor_);
#endif

    act_open_keymap_ = new QAction(tr("Open &Keymap..."), this);
    act_open_keymap_->setShortcut(QKeySequence("Ctrl+K"));
    connect(act_open_keymap_, &QAction::triggered,
            keymap_ctl_, &KeymapController::openKeymap);
    file_menu_->addAction(act_open_keymap_);

    act_rebind_ = new QAction(tr("Re&bind Keys (click a key, then press)"), this);
    act_rebind_->setCheckable(true);
    act_rebind_->setEnabled(false);  // enabled after a keymap is loaded
    connect(act_rebind_, &QAction::toggled,
            keymap_ctl_, &KeymapController::toggleRebind);
    file_menu_->addAction(act_rebind_);

    act_clear_ = new QAction(tr("C&lear Key Binding (press a key to remove)"), this);
    act_clear_->setCheckable(true);
    act_clear_->setEnabled(false);  // enabled after a keymap is loaded
    connect(act_clear_, &QAction::toggled,
            keymap_ctl_, &KeymapController::toggleClear);
    file_menu_->addAction(act_clear_);

    act_reset_keymap_ = new QAction(tr("Reset Keymap to &Default"), this);
    connect(act_reset_keymap_, &QAction::triggered,
            keymap_ctl_, &KeymapController::resetToDefault);
    file_menu_->addAction(act_reset_keymap_);

    // Presets: save the current layout under a name, switch between saved
    // layouts, or delete one. The controller owns the contents; we own the QMenu.
    preset_menu_ = file_menu_->addMenu(tr("Key&map Presets"));
    keymap_ctl_->setActions(act_rebind_, act_clear_, preset_menu_);
    keymap_ctl_->rebuildPresetMenu();

    file_menu_->addSeparator();

    act_settings_ = new QAction(tr("&Settings..."), this);
    act_settings_->setShortcut(QKeySequence("Ctrl+,"));
    connect(act_settings_, &QAction::triggered, this, &MainWindow::onOpenSettings);
    file_menu_->addAction(act_settings_);

    file_menu_->addSeparator();

    act_exit_ = new QAction(tr("E&xit"), this);
    act_exit_->setShortcut(QKeySequence("Ctrl+Q"));
    connect(act_exit_, &QAction::triggered, qApp, &QApplication::quit);
    file_menu_->addAction(act_exit_);

    rec_menu_ = menuBar()->addMenu(tr("&Record"));

    act_rec_start_ = new QAction(tr("&Start Recording"), this);
    act_rec_start_->setShortcut(QKeySequence("Ctrl+R"));
    connect(act_rec_start_, &QAction::triggered, this, &MainWindow::onStartRecording);
    rec_menu_->addAction(act_rec_start_);

    act_stop_ = new QAction(tr("&Stop"), this);
    act_stop_->setShortcut(QKeySequence("Ctrl+."));
    act_stop_->setEnabled(false);
    connect(act_stop_, &QAction::triggered, this, &MainWindow::onStop);
    rec_menu_->addAction(act_stop_);

    rec_menu_->addSeparator();

    act_playback_ = new QAction(tr("&Playback"), this);
    act_playback_->setShortcut(QKeySequence("Ctrl+P"));
    act_playback_->setEnabled(false);
    connect(act_playback_, &QAction::triggered, this, &MainWindow::onStartPlayback);
    rec_menu_->addAction(act_playback_);
}

void MainWindow::setupHelpMenu() {
    // Language submenu (mutually-exclusive radio items). English/简体中文 are
    // intentionally NOT translated — each is shown in its own language.
    help_menu_ = menuBar()->addMenu(tr("&Help"));

    lang_menu_ = help_menu_->addMenu(tr("&Language"));
    act_lang_en_ = lang_menu_->addAction(QStringLiteral("English"));
    act_lang_en_->setCheckable(true);
    connect(act_lang_en_, &QAction::triggered, this,
            [this] { setLanguage(Lang::English); });
    act_lang_zh_ = lang_menu_->addAction(QStringLiteral("简体中文"));
    act_lang_zh_->setCheckable(true);
    connect(act_lang_zh_, &QAction::triggered, this,
            [this] { setLanguage(Lang::Chinese); });
    act_lang_en_->setChecked(true);  // English is the default until loadLanguageSetting

    help_menu_->addSeparator();

    act_usage_guide_ = new QAction(tr("&Usage Guide..."), this);
    act_usage_guide_->setShortcut(QKeySequence("F1"));
    connect(act_usage_guide_, &QAction::triggered, this,
            &MainWindow::onShowUsageGuide);
    help_menu_->addAction(act_usage_guide_);

    act_about_ = new QAction(tr("&About keypiano..."), this);
    connect(act_about_, &QAction::triggered, this, &MainWindow::onShowAbout);
    help_menu_->addAction(act_about_);
}

void MainWindow::setupToolBar() {
    act_rec_start_->setIcon(QIcon(":/icons/record.png"));
    act_stop_->setIcon(QIcon(":/icons/stop.png"));
    act_playback_->setIcon(QIcon(":/icons/play.png"));

    toolbar_ = addToolBar(tr("Controls"));
    toolbar_->setMovable(false);
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar_->addAction(act_rec_start_);
    toolbar_->addAction(act_stop_);
    toolbar_->addSeparator();
    toolbar_->addAction(act_playback_);
}

void MainWindow::setupStatusBar() {
    lbl_sf2_name_ = new QLabel("SF2: (none)", this);
    lbl_cpu_      = new QLabel("CPU: --", this);
    lbl_latency_  = new QLabel("Latency: --", this);

    statusBar()->addWidget(lbl_sf2_name_);
    statusBar()->addPermanentWidget(lbl_cpu_);
    statusBar()->addPermanentWidget(new QLabel("|", this));
    statusBar()->addPermanentWidget(lbl_latency_);
}

void MainWindow::setupEngine() {
    synth_  = createFluidSynth();
    engine_ = std::make_unique<audio::AudioEngine>();

    audio::AudioEngine::Config cfg;  // defaults: 44100 Hz, 256 frames
    audio_cfg_ = cfg;
    if (!engine_->open(cfg, synth_.get())) {
        QMessageBox::warning(this, tr("Audio Error"),
                             tr("Failed to open audio output device.\n"
                                "SF2 playback will be unavailable."));
        engine_.reset();
        synth_.reset();
        recorder_ = std::make_unique<Recorder>([](const MidiEvent&) {});
        return;
    }

    recorder_ = std::make_unique<Recorder>(
        Recorder::makeAudioDispatch(engine_.get()));

    installHook();

    audio_bridge_ = std::make_unique<AudioBridge>(engine_.get());
    // Signals connected to PianoWidget in setupPianoWidget().
    audio_bridge_->start();
}

void MainWindow::installHook() {
    hook_ = KeyboardHook::create();
    bool ok = hook_->install(
        [this](const KeyEvent& kev) { handleKeyboardEvent(kev); });
    if (!ok) {
        QMessageBox::warning(this, tr("Hook Error"),
                             tr("Failed to install keyboard hook.\n"
                                "Keyboard input will be unavailable."));
        hook_.reset();
    }
}

void MainWindow::handleKeyboardEvent(const KeyEvent& kev) {
    // Rebind capture: if a piano key is waiting for a physical key, the controller
    // grabs this keydown (marshalling to the UI thread) instead of playing a note.
    if (kev.is_keydown && keymap_ctl_->tryCaptureRebind(kev.vk_code)) return;
    // Clear capture: in clear mode, the keydown removes that key's binding.
    if (kev.is_keydown && keymap_ctl_->tryCaptureClear(kev.vk_code)) return;

    // Read the immutable keymap snapshot — never the controller's working copy,
    // which the UI thread mutates concurrently. handle() also applies octave/
    // velocity/key-signature actions to ch0_/ch1_ (hook-owned) and flags Records.
    auto snap = keymap_ctl_->snapshot();
    if (!snap) return;
    KeyResult res = snap->handle(kev.vk_code, kev.is_keydown, ch0_, ch1_);

    if (res.toggle_record) {
        QMetaObject::invokeMethod(
            this, [this] { toggleRecordFromHotkey(); }, Qt::QueuedConnection);
    }
    if (!res.midi) return;
    MidiEvent ev = *res.midi;

    // Light the pedal lamp for a pedal CC (64/66/67). Marshalled to the UI thread.
    if (ev.type == EventType::ControlChange &&
        (ev.note == 64 || ev.note == 66 || ev.note == 67)) {
        const int  cc = ev.note;
        const bool on = ev.vel >= 64;
        QMetaObject::invokeMethod(
            this, [this, cc, on] {
                if (pedal_widget_) pedal_widget_->setPedalState(cc, on);
            },
            Qt::QueuedConnection);
    }

    if (recorder_ && recorder_->state() == Recorder::State::Recording) {
        using namespace std::chrono;
        ev.ts_us = duration_cast<microseconds>(
                       steady_clock::now() - record_start_)
                       .count();
        recorder_->onMidiEvent(ev);
    }

    // engine_ is only reassigned by stop/startEngine, which uninstall the hook
    // first — so it cannot be swapped out from under this callback.
    if (!engine_) return;
    switch (ev.type) {
        case EventType::NoteOn:
            engine_->postNoteOn(ev.chan, ev.note, ev.vel);
            break;
        case EventType::NoteOff:
            engine_->postNoteOff(ev.chan, ev.note);
            break;
        case EventType::ControlChange:
            engine_->postControlChange(ev.chan, ev.note, ev.vel);
            break;
        case EventType::AllNotesOff:
            engine_->postAllNotesOff(ev.chan);
            break;
    }
}

void MainWindow::toggleRecordFromHotkey() {
    if (!recorder_) return;
    if (recorder_->state() == Recorder::State::Recording)
        onStop();
    else if (recorder_->state() == Recorder::State::Idle)
        onStartRecording();
}

void MainWindow::restartEngine(const audio::AudioEngine::Config& cfg) {
    stopEngine();
    startEngine(cfg);
}

void MainWindow::stopEngine() {
    // Uninstall the hook FIRST: uninstall() joins the hook thread, guaranteeing
    // no callback is running (or will run) before we destroy the recorder_ and
    // engine_ that handleKeyboardEvent() dereferences. Tearing those down while a
    // callback was mid-flight would be a use-after-free on a backend switch.
    if (hook_) { hook_->uninstall(); hook_.reset(); }
    if (audio_bridge_) { audio_bridge_->stop(); audio_bridge_.reset(); }
    if (recorder_) {
        recorder_->stopPlayback();
        recorder_->stopRecording();
        recorder_.reset();
    }
    // engine_->close() blocks until the audio callback has stopped (and shuts
    // down the *current* synth_), so once this returns the caller may safely
    // swap or destroy synth_.
    if (engine_) { engine_->close(); engine_.reset(); }
}

void MainWindow::startEngine(const audio::AudioEngine::Config& cfg) {
    engine_ = std::make_unique<audio::AudioEngine>();
    if (!engine_->open(cfg, synth_.get())) {
        QMessageBox::critical(this, tr("Audio Error"),
                              tr("Failed to open audio with the new settings.\n"
                                 "Reverting to defaults."));
        engine_.reset();
        recorder_ = std::make_unique<Recorder>([](const MidiEvent&) {});
        return;
    }

    audio_cfg_ = cfg;
    recorder_ = std::make_unique<Recorder>(
        Recorder::makeAudioDispatch(engine_.get()));
    installHook();

    audio_bridge_ = std::make_unique<AudioBridge>(engine_.get());
    audio_bridge_->start();

    // Reconnect PianoWidget signals to the new bridge.
    if (piano_widget_ && audio_bridge_) {
        connect(audio_bridge_.get(), &AudioBridge::noteActivated,
                piano_widget_,       &PianoWidget::onNoteActivated);
        connect(audio_bridge_.get(), &AudioBridge::noteReleased,
                piano_widget_,       &PianoWidget::onNoteReleased);
        connect(piano_widget_, &PianoWidget::mouseNoteOn,
                this, [this](int midi, int vel) {
                    engine_->postNoteOn(0,
                        static_cast<uint8_t>(midi),
                        static_cast<uint8_t>(vel));
                });
        connect(piano_widget_, &PianoWidget::mouseNoteOff,
                this, [this](int midi) {
                    engine_->postNoteOff(0, static_cast<uint8_t>(midi));
                });
    }
}

void MainWindow::setupPianoWidget() {
    piano_widget_ = new PianoWidget(this);
    pedal_widget_ = new PedalIndicatorWidget(this);

    // Central widget = piano on top, pedal indicator strip below it.
    auto* central = new QWidget(this);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(piano_widget_, /*stretch=*/1);
    col->addWidget(pedal_widget_, /*stretch=*/0);
    setCentralWidget(central);

    if (audio_bridge_) {
        connect(audio_bridge_.get(), &AudioBridge::noteActivated,
                piano_widget_,       &PianoWidget::onNoteActivated);
        connect(audio_bridge_.get(), &AudioBridge::noteReleased,
                piano_widget_,       &PianoWidget::onNoteReleased);
    }

    if (engine_) {
        connect(piano_widget_, &PianoWidget::mouseNoteOn,
                this, [this](int midi, int vel) {
                    engine_->postNoteOn(0,
                        static_cast<uint8_t>(midi),
                        static_cast<uint8_t>(vel));
                });
        connect(piano_widget_, &PianoWidget::mouseNoteOff,
                this, [this](int midi) {
                    engine_->postNoteOff(0, static_cast<uint8_t>(midi));
                });
    }

    connect(piano_widget_, &PianoWidget::keyClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindKeyClicked);
    connect(pedal_widget_, &PedalIndicatorWidget::pedalClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindPedalClicked);

    overlay_ = new KeyboardOverlayWidget(piano_widget_);

    // The controller drives the overlay + pedal lamps + selection highlight.
    keymap_ctl_->setWidgets(piano_widget_, overlay_, pedal_widget_);
}

void MainWindow::loadDefaultSoundFont() {
    // FluidSynth's sfload() needs a real file path; the SF2 is an embedded Qt
    // resource, so extract it to a per-user cache file, then load. This gives an
    // audible piano on first launch without bundling a loose file the user could
    // lose. (They can still Open SF2 / Open VST3 to change it.)
    if (!synth_ || current_backend_ != Backend::FluidSynth) return;

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString sf2_path = dir + "/default_piano.sf2";

    // Always re-extract so the cached file tracks the embedded resource — an old
    // build's stale SF2 must never shadow an updated default sound.
    {
        QFile src(":/soundfonts/default_piano.sf2");
        if (!src.open(QIODevice::ReadOnly)) return;
        QFile dst(sf2_path);
        if (!dst.open(QIODevice::WriteOnly)) return;
        dst.write(src.readAll());
        dst.close();
    }

    if (synth_->loadInstrument(sf2_path.toStdString())) {
        current_sf2_name_ = "default_piano.sf2";
        sf2_builtin_ = true;
        refreshSf2Label();
    }
}

// Rebuilds the status-bar instrument label from the current backend + name. Kept
// in one place so a language switch can refresh the " (built-in)" suffix.
void MainWindow::refreshSf2Label() {
    if (!lbl_sf2_name_) return;
    const QString prefix =
        (current_backend_ == Backend::Vst3) ? "VST3: " : "SF2: ";
    QString text = prefix + current_sf2_name_;
    if (sf2_builtin_) text += tr(" (built-in)");
    lbl_sf2_name_->setText(text);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void MainWindow::syncRecordActions() {
    auto state = recorder_ ? recorder_->state() : Recorder::State::Idle;
    bool idle  = (state == Recorder::State::Idle);
    bool busy  = (state == Recorder::State::Recording) ||
                 (state == Recorder::State::Playing);

    act_rec_start_->setEnabled(idle);
    act_stop_->setEnabled(busy);
    act_playback_->setEnabled(idle && recorder_ &&
                              recorder_->eventCount() > 0);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onOpenSf2() {
    SoundFontDialog dlg(this);
    dlg.setInitialPath(current_sf2_path_);  // full path, or empty for built-in
    if (dlg.exec() != QDialog::Accepted) return;

    const QString path = dlg.selectedPath();
    if (path.isEmpty()) return;

    // Switch back to the FluidSynth backend if a VST3 plug-in was active.
    if (current_backend_ != Backend::FluidSynth) {
        closePluginEditor();   // detach before the VST3 synth_ is destroyed
        stopEngine();          // stop the audio callback BEFORE swapping synth_
        synth_ = createFluidSynth();
        current_backend_ = Backend::FluidSynth;
        startEngine(audio_cfg_);
        updateEditorAction();
    }
    if (!engine_ || !synth_) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Audio engine is not running."));
        return;
    }

    if (!synth_->loadInstrument(path.toStdString())) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to load SoundFont:\n%1").arg(path));
        return;
    }
    current_sf2_name_ = QFileInfo(path).fileName();
    current_sf2_path_ = path;
    sf2_builtin_ = false;
    refreshSf2Label();
    statusBar()->showMessage(tr("Loaded: %1").arg(current_sf2_name_), 3000);
}

void MainWindow::onOpenVst3() {
#ifdef KEYPIANO_ENABLE_VST3
    // VST3 plug-ins on Windows are bundle folders ("Foo.vst3/Contents/..."),
    // so the user picks the .vst3 folder itself. The dialog opens at the
    // standard VST3 install location (or wherever they last browsed) so the
    // installed plug-ins are right there.
    QString path = QFileDialog::getExistingDirectory(
        this, tr("Select VST3 instrument (.vst3 bundle folder)"), vst3StartDir());
    if (path.isEmpty()) return;

    // Detach any open editor before the current synth_ is destroyed.
    closePluginEditor();

    // Stop the audio callback BEFORE swapping synth_ (else the callback would
    // dereference the freed old backend), then bring the engine back up.
    stopEngine();
    synth_ = createVst3Synth();
    current_backend_ = Backend::Vst3;
    startEngine(audio_cfg_);
    if (!engine_ || !synth_) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to start audio with the VST3 backend."));
        return;
    }

    if (!synth_->loadInstrument(path.toStdString())) {
        QMessageBox::critical(
            this, tr("Error"),
            tr("Failed to load VST3 instrument:\n%1\n\n"
               "Make sure the folder is a valid .vst3 bundle containing an "
               "instrument plug-in.").arg(path));
        return;
    }
    // Remember the parent folder so next time the dialog opens right here.
    QSettings("keypiano", "keypiano")
        .setValue(kVst3DirKey, QFileInfo(path).absolutePath());

    current_sf2_name_ = QFileInfo(path).fileName();
    sf2_builtin_ = false;
    refreshSf2Label();
    statusBar()->showMessage(tr("Loaded VST3: %1").arg(current_sf2_name_), 3000);
    updateEditorAction();
#endif
}

void MainWindow::onShowPluginEditor() {
    if (editor_window_) {            // already open — bring it forward
        editor_window_->show();
        editor_window_->raise();
        editor_window_->activateWindow();
        return;
    }

    auto* editor = dynamic_cast<IPluginEditor*>(synth_.get());
    if (!editor || !editor->hasEditor()) {
        QMessageBox::information(this, tr("Plugin Editor"),
                                 tr("The current backend has no plug-in editor."));
        return;
    }

    auto* win = new Vst3EditorWindow(editor, this);
    if (!win->openEditor()) {
        delete win;
        QMessageBox::information(
            this, tr("Plugin Editor"),
            tr("This plug-in does not provide a custom editor window."));
        return;
    }
    editor_window_ = win;
    connect(win, &QObject::destroyed, this,
            [this] { editor_window_ = nullptr; });
    win->show();
}

void MainWindow::closePluginEditor() {
    if (editor_window_) {
        // close() runs the window's closeEvent synchronously (detaching the
        // plug-in view) before WA_DeleteOnClose schedules deletion.
        editor_window_->close();
        editor_window_ = nullptr;
    }
}

void MainWindow::updateEditorAction() {
    if (!act_show_editor_) return;
    auto* editor = dynamic_cast<IPluginEditor*>(synth_.get());
    act_show_editor_->setEnabled(editor && editor->hasEditor());
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(audio_cfg_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto new_cfg = dlg.config();
    if (new_cfg.device_name  == audio_cfg_.device_name  &&
        new_cfg.sample_rate  == audio_cfg_.sample_rate   &&
        new_cfg.buffer_frames == audio_cfg_.buffer_frames)
        return;
    restartEngine(new_cfg);
    statusBar()->showMessage(tr("Audio settings applied."), 3000);
}

void MainWindow::onStartRecording() {
    if (!recorder_) return;
    record_start_ = std::chrono::steady_clock::now();
    recorder_->startRecording();
    syncRecordActions();
    statusBar()->showMessage(tr("Recording..."));
}

void MainWindow::onStop() {
    if (!recorder_) return;

    auto state = recorder_->state();
    if (state == Recorder::State::Playing) {
        recorder_->stopPlayback();
        syncRecordActions();
        statusBar()->showMessage(tr("Playback stopped."), 3000);
        return;
    }

    recorder_->stopRecording();
    syncRecordActions();

    auto n = static_cast<int>(recorder_->eventCount());
    statusBar()->showMessage(tr("Recording stopped — %1 events.").arg(n));

    if (n == 0) return;

    auto reply = QMessageBox::question(
        this, tr("Save Recording"),
        tr("Save %1 event(s) to file?").arg(n),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Recording"), {},
        tr("keypiano performance (*.kps);;All files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".kps", Qt::CaseInsensitive)) path += ".kps";

    KpsMeta meta;
    meta.title = current_sf2_name_.toStdString();
    std::string err;
    if (!recorder_->saveToFile(path.toStdString(), meta, &err)) {
        QMessageBox::critical(this, tr("Save Error"),
                              tr("Failed to save:\n%1")
                                  .arg(QString::fromStdString(err)));
    } else {
        statusBar()->showMessage(
            tr("Saved: %1").arg(QFileInfo(path).fileName()), 5000);
    }
}

void MainWindow::onStartPlayback() {
    if (!recorder_) return;
    if (!recorder_->startPlayback()) {
        QMessageBox::information(this, tr("Playback"),
                                 tr("Nothing to play back.\n"
                                    "Record something first."));
        return;
    }
    syncRecordActions();
    statusBar()->showMessage(
        tr("Playing back %1 event(s)...")
            .arg(static_cast<int>(recorder_->eventCount())));
}

void MainWindow::updateStatus() {
    if (engine_ && engine_->isOpen()) {
        const auto& s = engine_->stats();
        uint32_t lat = s.latency_us.load(std::memory_order_relaxed);
        double   cpu = s.cpu_load.load(std::memory_order_relaxed);
        lbl_latency_->setText(
            tr("Latency: %1 ms").arg(lat / 1000.0, 0, 'f', 1));
        lbl_cpu_->setText(
            tr("CPU: %1%").arg(cpu * 100.0, 0, 'f', 1));
    }
    syncRecordActions();
}

// ── Language / Help ─────────────────────────────────────────────────────────────

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::setLanguage(Lang lang) {
    lang_ = lang;
    if (lang == Lang::Chinese) {
        if (!translator_) translator_ = new Translator(qApp);
        qApp->installTranslator(translator_);  // posts a LanguageChange event
    } else if (translator_) {
        qApp->removeTranslator(translator_);    // also posts LanguageChange
    }

    // Reflect the choice in the radio items and persist it for next launch.
    if (act_lang_en_) act_lang_en_->setChecked(lang == Lang::English);
    if (act_lang_zh_) act_lang_zh_->setChecked(lang == Lang::Chinese);
    QSettings("keypiano", "keypiano")
        .setValue("language", lang == Lang::Chinese ? "zh" : "en");

    // The posted LanguageChange reaches us asynchronously; refresh now too so the
    // menus update deterministically even before the event loop spins.
    retranslateUi();
}

void MainWindow::loadLanguageSetting() {
    const QString saved =
        QSettings("keypiano", "keypiano").value("language", "en").toString();
    setLanguage(saved == "zh" ? Lang::Chinese : Lang::English);
}

void MainWindow::retranslateUi() {
    // Menus and submenus.
    if (file_menu_)   file_menu_->setTitle(tr("&File"));
    if (rec_menu_)    rec_menu_->setTitle(tr("&Record"));
    if (help_menu_)   help_menu_->setTitle(tr("&Help"));
    if (lang_menu_)   lang_menu_->setTitle(tr("&Language"));
    if (preset_menu_) preset_menu_->setTitle(tr("Key&map Presets"));
    if (toolbar_)     toolbar_->setWindowTitle(tr("Controls"));

    // File menu actions.
    if (act_open_sf2_)    act_open_sf2_->setText(tr("Open SF&2..."));
    if (act_open_vst3_)   act_open_vst3_->setText(tr("Open &VST3 Instrument..."));
    if (act_show_editor_) act_show_editor_->setText(tr("Show Plugin &Editor..."));
    if (act_open_keymap_) act_open_keymap_->setText(tr("Open &Keymap..."));
    if (act_rebind_)
        act_rebind_->setText(tr("Re&bind Keys (click a key, then press)"));
    if (act_clear_)
        act_clear_->setText(tr("C&lear Key Binding (press a key to remove)"));
    if (act_reset_keymap_)
        act_reset_keymap_->setText(tr("Reset Keymap to &Default"));
    if (act_settings_)    act_settings_->setText(tr("&Settings..."));
    if (act_exit_)        act_exit_->setText(tr("E&xit"));

    // Record menu actions.
    if (act_rec_start_) act_rec_start_->setText(tr("&Start Recording"));
    if (act_stop_)      act_stop_->setText(tr("&Stop"));
    if (act_playback_)  act_playback_->setText(tr("&Playback"));

    // Help menu actions.
    if (act_usage_guide_) act_usage_guide_->setText(tr("&Usage Guide..."));
    if (act_about_)       act_about_->setText(tr("&About keypiano..."));

    // Dynamic submenu (re-tr()'s "(no saved presets)", "Save Current As...", ...).
    if (keymap_ctl_) keymap_ctl_->rebuildPresetMenu();
    if (pedal_widget_) pedal_widget_->update();  // repaint pedal names in new lang

    // Status-bar instrument label (refreshes the " (built-in)" suffix). Latency
    // and CPU labels are refreshed on the next 500 ms status tick.
    refreshSf2Label();
}

void MainWindow::onShowUsageGuide() {
    QDialog dlg(this);
    dlg.setWindowTitle(lang_ == Lang::Chinese ? QStringLiteral("使用说明")
                                              : QStringLiteral("Usage Guide"));
    dlg.resize(680, 560);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(true);
    browser->setHtml(lang_ == Lang::Chinese ? usageGuideHtmlZh()
                                            : usageGuideHtmlEn());
    layout->addWidget(browser);
    dlg.exec();
}

void MainWindow::onShowAbout() {
    if (lang_ == Lang::Chinese) {
        QMessageBox::about(
            this, QStringLiteral("关于 keypiano"),
            QStringLiteral(
                "<h3>keypiano 1.0.0</h3>"
                "<p>把电脑键盘变成 MIDI 钢琴的轻量工具。</p>"
                "<p>内置 FluidSynth 软件合成器，也可加载你自己的 SF2 音色库或 "
                "VST3 乐器插件。</p>"
                "<p>以 GPL v3 协议开源发布。</p>"));
    } else {
        QMessageBox::about(
            this, QStringLiteral("About keypiano"),
            QStringLiteral(
                "<h3>keypiano 1.0.0</h3>"
                "<p>A lightweight tool that turns your computer keyboard into a "
                "MIDI piano.</p>"
                "<p>Ships with the FluidSynth software synth, and can also load "
                "your own SF2 SoundFonts or VST3 instrument plug-ins.</p>"
                "<p>Released as open source under the GPL v3 license.</p>"));
    }
}

} // namespace keypiano::ui
