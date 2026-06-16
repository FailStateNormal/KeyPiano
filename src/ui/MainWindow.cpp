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
#include "RecordingController.h"
#include "bridge/AudioBridge.h"
#include "widgets/PianoWidget.h"
#include "widgets/KeyboardOverlayWidget.h"
#include "widgets/PedalIndicatorWidget.h"
#include "widgets/Vst3EditorWindow.h"
#include "dialogs/SoundFontDialog.h"
#include "dialogs/SettingsDialog.h"
#include "recorder/Recorder.h"
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

    for (auto& e : pedal_engaged_) e.store(false, std::memory_order_relaxed);

    keymap_ctl_ = new KeymapController(this, this);
    connect(keymap_ctl_, &KeymapController::status, this,
            [this](const QString& msg, int timeout) {
                if (msg.isEmpty()) statusBar()->clearMessage();
                else               statusBar()->showMessage(msg, timeout);
            });

    rec_ctl_ = new RecordingController(this, this);
    connect(rec_ctl_, &RecordingController::status, this,
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
    loadPedalModeSetting();
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
    // Uninstall the hook first (joins its thread) so no feed() runs while we
    // release the recorder; then drop the recorder before engine_ closes, so the
    // recorder (whose playback dispatch targets engine_) is gone first.
    if (hook_)   hook_->uninstall();
    if (audio_bridge_) audio_bridge_->stop();
    if (rec_ctl_) rec_ctl_->clearRecorder();
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

    // Pedal behaviour: Hold (momentary) vs Toggle (press on/off).
    pedal_mode_menu_ = file_menu_->addMenu(tr("&Pedal Mode"));
    act_pedal_hold_ = pedal_mode_menu_->addAction(tr("&Hold (press and release)"));
    act_pedal_hold_->setCheckable(true);
    connect(act_pedal_hold_, &QAction::triggered, this,
            [this] { setPedalMode(PedalMode::Hold); });
    act_pedal_toggle_ =
        pedal_mode_menu_->addAction(tr("&Toggle (press once on, again off)"));
    act_pedal_toggle_->setCheckable(true);
    connect(act_pedal_toggle_, &QAction::triggered, this,
            [this] { setPedalMode(PedalMode::Toggle); });
    act_pedal_hold_->setChecked(true);  // default until loadPedalModeSetting

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
    connect(act_rec_start_, &QAction::triggered,
            rec_ctl_, &RecordingController::startRecording);
    rec_menu_->addAction(act_rec_start_);

    act_stop_ = new QAction(tr("&Stop"), this);
    act_stop_->setShortcut(QKeySequence("Ctrl+."));
    act_stop_->setEnabled(false);
    connect(act_stop_, &QAction::triggered, rec_ctl_, &RecordingController::stop);
    rec_menu_->addAction(act_stop_);

    rec_menu_->addSeparator();

    act_playback_ = new QAction(tr("&Playback"), this);
    act_playback_->setShortcut(QKeySequence("Ctrl+P"));
    act_playback_->setEnabled(false);
    connect(act_playback_, &QAction::triggered,
            rec_ctl_, &RecordingController::startPlayback);
    rec_menu_->addAction(act_playback_);

    // Open a previously saved .kps recording and play it (independent of whatever
    // is currently in the buffer). Always available — it loads then plays.
    act_open_rec_ = new QAction(tr("&Open Recording..."), this);
    act_open_rec_->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(act_open_rec_, &QAction::triggered,
            rec_ctl_, &RecordingController::openRecording);
    rec_menu_->addAction(act_open_rec_);

    // The controller owns the enabled-state logic for these three actions.
    rec_ctl_->setActions(act_rec_start_, act_stop_, act_playback_);
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
    // No F1 shortcut: F1 is a playing key (octave down) in the default keymap, and
    // the global hook forwards keys to Qt, so an F1 accelerator would fire the
    // guide *and* shift the octave on every press.
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
    lbl_drops_    = new QLabel(this);
    lbl_drops_->setStyleSheet("color: red;");
    lbl_drops_->setVisible(false);  // shown only when events are actually dropped

    statusBar()->addWidget(lbl_sf2_name_);
    statusBar()->addPermanentWidget(lbl_drops_);
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
        rec_ctl_->setRecorder(std::make_unique<Recorder>([](const MidiEvent&) {}));
        return;
    }

    rec_ctl_->setRecorder(std::make_unique<Recorder>(
        Recorder::makeAudioDispatch(engine_.get())));

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

uint8_t MainWindow::softVelocity(int vel) const {
    // When the soft pedal is engaged, scale velocity to 55%. With the default
    // SF2's steep velocity→loudness curve this lands around 30% of full loudness
    // — a clear una corda, ~2.5x louder than the earlier 35% setting (which was
    // too quiet) but still distinctly softer than normal.
    if (!pedal_engaged_[2].load(std::memory_order_relaxed))
        return static_cast<uint8_t>(vel);
    int v = vel * 55 / 100;
    return static_cast<uint8_t>(v < 1 ? 1 : v);
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
            rec_ctl_, &RecordingController::toggleFromHotkey,
            Qt::QueuedConnection);
    }
    if (!res.midi) return;
    MidiEvent ev = *res.midi;

    // ── Pedals ──────────────────────────────────────────────────────────────
    // handle() emits a pedal CC as 127 on key-down and 0 on key-up (momentary).
    // Apply the chosen mode and track each pedal's engaged state here.
    if (const int idx = pedalIndex(ev.type == EventType::ControlChange ? ev.note : -1);
        idx >= 0) {
        bool engage;
        if (pedal_mode_.load(std::memory_order_relaxed) == PedalMode::Toggle) {
            if (!kev.is_keydown) return;  // toggle acts on press only; drop release
            engage = !pedal_engaged_[idx].load(std::memory_order_relaxed);
        } else {
            engage = kev.is_keydown;      // hold: down engages, up releases
        }
        pedal_engaged_[idx].store(engage, std::memory_order_relaxed);
        ev.vel = engage ? 127 : 0;        // CC value the synth receives

        const int cc = ev.note;
        QMetaObject::invokeMethod(
            this, [this, cc, engage] {
                if (pedal_widget_) pedal_widget_->setPedalState(cc, engage);
            },
            Qt::QueuedConnection);
        // Note: CC67 (soft) is sent on too, but FluidSynth ignores it — the soft
        // effect is applied below as a velocity reduction on new notes.

        // VST3 instruments get no sustain/sostenuto: controlChange is a no-op on
        // that backend (no IMidiMapping), so the pedal lamp lights but nothing is
        // held. Tell the user on engage so they don't think the pedal is broken.
        // (current_backend_ is safe to read here: backend switches uninstall the
        // hook first, so it never changes while this callback runs.)
        if (engage && current_backend_ == Backend::Vst3 && (idx == 0 || idx == 1)) {
            QMetaObject::invokeMethod(
                this, [this] {
                    statusBar()->showMessage(
                        tr("Sustain/sostenuto pedals aren't supported by VST3 "
                           "instruments (the soft pedal still works)."),
                        4000);
                },
                Qt::QueuedConnection);
        }
    }

    // Soft pedal (engaged) softens new notes, since the synth does not.
    if (ev.type == EventType::NoteOn)
        ev.vel = softVelocity(ev.vel);

    // Record the event (no-op unless recording). The controller timestamps and
    // appends it; same hook-thread safety invariant as before the extraction.
    rec_ctl_->feed(ev);

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

void MainWindow::restartEngine(const audio::AudioEngine::Config& cfg) {
    stopEngine();
    startEngine(cfg);
}

void MainWindow::stopEngine() {
    // Uninstall the hook FIRST: uninstall() joins the hook thread, guaranteeing
    // no callback is running (or will run) before we destroy the recorder and
    // engine_ that handleKeyboardEvent() dereferences. Tearing those down while a
    // callback was mid-flight would be a use-after-free on a backend switch.
    if (hook_) { hook_->uninstall(); hook_.reset(); }
    if (audio_bridge_) { audio_bridge_->stop(); audio_bridge_.reset(); }
    rec_ctl_->clearRecorder();  // stops playback/recording + releases the recorder
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
                                 "Audio is stopped — reopen it from Settings."));
        engine_.reset();
        rec_ctl_->setRecorder(std::make_unique<Recorder>([](const MidiEvent&) {}));
        return;
    }

    audio_cfg_ = cfg;
    rec_ctl_->setRecorder(std::make_unique<Recorder>(
        Recorder::makeAudioDispatch(engine_.get())));
    installHook();

    // The new synth starts with no pedals down — clear our state + lamps to match.
    for (int i = 0; i < 3; ++i) {
        pedal_engaged_[i].store(false, std::memory_order_relaxed);
        static const int cc[3] = {64, 66, 67};
        if (pedal_widget_) pedal_widget_->setPedalState(cc[i], false);
    }

    audio_bridge_ = std::make_unique<AudioBridge>(engine_.get());
    audio_bridge_->start();

    // Reconnect the bridge → PianoWidget signals: the bridge is a fresh object
    // each restart, so this targets a new sender (no duplication). Mouse-click
    // playback is wired ONCE in setupPianoWidget and must NOT be reconnected here
    // — its target (this) is persistent, so reconnecting would stack a duplicate
    // on every restart and play multiple NoteOns per click.
    if (piano_widget_ && audio_bridge_) {
        connect(audio_bridge_.get(), &AudioBridge::noteActivated,
                piano_widget_,       &PianoWidget::onNoteActivated);
        connect(audio_bridge_.get(), &AudioBridge::noteReleased,
                piano_widget_,       &PianoWidget::onNoteReleased);
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

    // Mouse-click playback is wired exactly once, here — never in startEngine,
    // which runs on every backend/settings change and would stack duplicate
    // connections. The lambdas read engine_ live so they follow restarts, and
    // no-op while the engine is down (e.g. if it failed to open at startup).
    // They also feed the recorder (like the keyboard path) so on-screen clicks
    // are captured in a recording, not just the audio output.
    connect(piano_widget_, &PianoWidget::mouseNoteOn,
            this, [this](int midi, int vel) {
                const auto note = static_cast<uint8_t>(midi);
                const auto v    = softVelocity(vel);  // soft pedal applies to clicks too
                rec_ctl_->feed(MidiEvent{EventType::NoteOn, 0, note, v, 0});
                if (engine_) engine_->postNoteOn(0, note, v);
            });
    connect(piano_widget_, &PianoWidget::mouseNoteOff,
            this, [this](int midi) {
                const auto note = static_cast<uint8_t>(midi);
                rec_ctl_->feed(MidiEvent{EventType::NoteOff, 0, note, 0, 0});
                if (engine_) engine_->postNoteOff(0, note);
            });

    connect(piano_widget_, &PianoWidget::keyClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindKeyClicked);
    connect(pedal_widget_, &PedalIndicatorWidget::pedalClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindPedalClicked);

    overlay_ = new KeyboardOverlayWidget(piano_widget_);

    // The controller drives the overlay + pedal lamps + selection highlight.
    keymap_ctl_->setWidgets(piano_widget_, overlay_, pedal_widget_);
}

void MainWindow::loadDefaultSoundFont() {
    // FluidSynth's sfload() needs a real file path. The preferred default sound
    // is the bundled GeneralUser GS piano — a real sampled piano shipped as a
    // loose file beside the exe (too large, ~30 MB, for the .qrc). If it is
    // missing we fall back to the tiny synthetic piano embedded in the .qrc so
    // the app is always audible out of the box. (Open SF2 / Open VST3 override
    // either at runtime.)
    if (!synth_ || current_backend_ != Backend::FluidSynth) return;

    // 1) Preferred: the bundled GeneralUser GS sampled piano next to the exe.
    const QString bundled = bundledDefaultSf2Path();
    if (!bundled.isEmpty() &&
        synth_->loadInstrument(bundled.toStdString())) {
        current_sf2_name_ = "GeneralUser-GS.sf2";
        sf2_builtin_ = true;
        current_sf2_path_.clear();  // a default, not a user pick — keep it out of recents
        refreshSf2Label();
        return;
    }

    // 2) Fallback: extract the embedded synthetic piano to a per-user cache file
    // and load it. Always re-extract so an old build's stale SF2 never shadows
    // an updated default sound.
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
        current_sf2_name_ = "default_piano.sf2";
        sf2_builtin_ = true;
        refreshSf2Label();
    }
}

QString MainWindow::bundledDefaultSf2Path() const {
    const QString p =
        QCoreApplication::applicationDirPath() + "/soundfonts/GeneralUser-GS.sf2";
    return QFileInfo::exists(p) ? p : QString();
}

// Rebuilds the status-bar instrument label from the current backend + name. Kept
// in one place so a language switch can refresh the " (built-in)" suffix.
void MainWindow::refreshSf2Label() {
    // Keep the recorder's save-file [meta] title in sync with the instrument
    // (called on every instrument change). Done here so there is one update point.
    if (rec_ctl_) rec_ctl_->setRecordingTitle(current_sf2_name_);

    if (!lbl_sf2_name_) return;
    const QString prefix =
        (current_backend_ == Backend::Vst3) ? "VST3: " : "SF2: ";
    QString text = prefix + current_sf2_name_;
    if (sf2_builtin_) text += tr(" (built-in)");
    lbl_sf2_name_->setText(text);
}

// ── Transactional backend switching ─────────────────────────────────────────

bool MainWindow::rebuildBackend(Backend target) {
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
    startEngine(audio_cfg_);
    return engine_ != nullptr && synth_ != nullptr;
}

MainWindow::InstrumentState MainWindow::currentInstrument() const {
    InstrumentState s;
    s.backend = current_backend_;
    s.name    = current_sf2_name_;
    s.builtin = sf2_builtin_;
    s.path    = (current_backend_ == Backend::Vst3) ? current_vst3_path_
                                                    : current_sf2_path_;
    return s;
}

void MainWindow::restoreInstrument(const InstrumentState& s) {
    bool ok = false;
    if (s.backend == Backend::Vst3 && !s.path.isEmpty()) {
        ok = rebuildBackend(Backend::Vst3) && synth_ &&
             synth_->loadInstrument(s.path.toStdString());
        if (ok) {
            current_vst3_path_ = s.path;
            current_sf2_name_  = s.name;
            current_sf2_path_.clear();
            sf2_builtin_       = false;
        }
    } else if (!s.builtin && !s.path.isEmpty()) {  // FluidSynth + a user SF2
        ok = rebuildBackend(Backend::FluidSynth) && synth_ &&
             synth_->loadInstrument(s.path.toStdString());
        if (ok) {
            current_sf2_path_  = s.path;
            current_sf2_name_  = s.name;
            current_vst3_path_.clear();
            sf2_builtin_       = false;
        }
    }
    // Built-in default, or any restore that failed above: fall back to the
    // bundled default piano so the user always has a working instrument.
    if (!ok) {
        rebuildBackend(Backend::FluidSynth);
        current_vst3_path_.clear();
        loadDefaultSoundFont();  // sets name + builtin flag + label
    }
    refreshSf2Label();
    updateEditorAction();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onOpenSf2() {
    SoundFontDialog dlg(this);
    // Pin the bundled GeneralUser GS piano so the user can always pick it back,
    // even though it's never stored in the recent list.
    if (const QString bundled = bundledDefaultSf2Path(); !bundled.isEmpty())
        dlg.setBuiltinDefault(bundled, QStringLiteral("GeneralUser-GS.sf2"));
    dlg.setInitialPath(current_sf2_path_);  // full path, or empty for built-in
    if (dlg.exec() != QDialog::Accepted) return;

    const QString path = dlg.selectedPath();
    if (path.isEmpty()) return;

    if (current_backend_ != Backend::FluidSynth) {
        // Coming from a VST3 plug-in: rebuild on FluidSynth, then load. If the
        // load fails, roll back to the previous backend rather than strand the
        // user on an empty FluidSynth.
        const InstrumentState prev = currentInstrument();
        if (!(rebuildBackend(Backend::FluidSynth) && synth_ &&
              synth_->loadInstrument(path.toStdString()))) {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to load SoundFont:\n%1").arg(path));
            restoreInstrument(prev);
            return;
        }
    } else {
        // Already on FluidSynth: load on the live synth. loadInstrument() loads
        // the new font before unloading the old, so a failed load leaves the
        // current instrument playing — no teardown, no audio gap.
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
    }

    // If the user picked the bundled default, keep our state consistent with
    // loadDefaultSoundFont(): mark it built-in and keep it out of the recents path.
    const QString bundled = bundledDefaultSf2Path();
    const bool is_builtin =
        !bundled.isEmpty() &&
        QFileInfo(path).canonicalFilePath() == QFileInfo(bundled).canonicalFilePath();

    current_sf2_name_ = QFileInfo(path).fileName();
    current_sf2_path_ = is_builtin ? QString() : path;
    current_vst3_path_.clear();
    sf2_builtin_ = is_builtin;
    refreshSf2Label();
    updateEditorAction();
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

    // Snapshot the current working instrument so a bad bundle can be rolled back.
    const InstrumentState prev = currentInstrument();

    if (rebuildBackend(Backend::Vst3) && synth_ &&
        synth_->loadInstrument(path.toStdString())) {
        // Success — remember the bundle + its parent folder and update the label.
        current_vst3_path_ = path;
        current_sf2_name_  = QFileInfo(path).fileName();
        current_sf2_path_.clear();
        sf2_builtin_       = false;
        QSettings("keypiano", "keypiano")
            .setValue(kVst3DirKey, QFileInfo(path).absolutePath());
        refreshSf2Label();
        updateEditorAction();
        statusBar()->showMessage(tr("Loaded VST3: %1").arg(current_sf2_name_),
                                 3000);
        return;
    }

    // Load failed — the VST3 backend came up empty (or could not start). Restore
    // the previous, working backend so the user is never left without sound.
    QMessageBox::critical(
        this, tr("Error"),
        tr("Failed to load VST3 instrument:\n%1\n\n"
           "Make sure the folder is a valid .vst3 bundle containing an "
           "instrument plug-in.").arg(path));
    restoreInstrument(prev);
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

void MainWindow::updateStatus() {
    if (engine_ && engine_->isOpen()) {
        const auto& s = engine_->stats();
        uint32_t lat = s.latency_us.load(std::memory_order_relaxed);
        double   cpu = s.cpu_load.load(std::memory_order_relaxed);
        lbl_latency_->setText(
            tr("Latency: %1 ms").arg(lat / 1000.0, 0, 'f', 1));
        lbl_cpu_->setText(
            tr("CPU: %1%").arg(cpu * 100.0, 0, 'f', 1));

        // Surface dropped events only when they happen — a non-zero count means
        // the audio thread is overrunning (lost input notes) or the UI is behind
        // (missed key highlights). Stays hidden during normal operation.
        const uint32_t in_drops = s.event_drops.load(std::memory_order_relaxed);
        const uint32_t ui_drops = s.feedback_drops.load(std::memory_order_relaxed);
        if (in_drops || ui_drops) {
            lbl_drops_->setText(
                tr("Drops: %1 in / %2 ui").arg(in_drops).arg(ui_drops));
            lbl_drops_->setVisible(true);
        } else {
            lbl_drops_->setVisible(false);
        }
    }
    rec_ctl_->syncActions();
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

void MainWindow::setPedalMode(PedalMode mode) {
    pedal_mode_.store(mode, std::memory_order_relaxed);

    // Release any pedal that might be latched, so switching modes never strands a
    // pedal "down" on the synth (and stops soft-pedal velocity scaling).
    static const int cc[3] = {64, 66, 67};
    for (int i = 0; i < 3; ++i) {
        if (pedal_engaged_[i].exchange(false, std::memory_order_relaxed)) {
            if (engine_)
                engine_->postControlChange(0, static_cast<uint8_t>(cc[i]), 0);
            if (pedal_widget_) pedal_widget_->setPedalState(cc[i], false);
        }
    }

    if (act_pedal_hold_)   act_pedal_hold_->setChecked(mode == PedalMode::Hold);
    if (act_pedal_toggle_) act_pedal_toggle_->setChecked(mode == PedalMode::Toggle);
    QSettings("keypiano", "keypiano")
        .setValue("pedal_mode", mode == PedalMode::Toggle ? "toggle" : "hold");
}

void MainWindow::loadPedalModeSetting() {
    const QString s = QSettings("keypiano", "keypiano")
                          .value("pedal_mode", "hold").toString();
    setPedalMode(s == "toggle" ? PedalMode::Toggle : PedalMode::Hold);
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
    if (pedal_mode_menu_) pedal_mode_menu_->setTitle(tr("&Pedal Mode"));
    if (act_pedal_hold_)
        act_pedal_hold_->setText(tr("&Hold (press and release)"));
    if (act_pedal_toggle_)
        act_pedal_toggle_->setText(tr("&Toggle (press once on, again off)"));
    if (act_settings_)    act_settings_->setText(tr("&Settings..."));
    if (act_exit_)        act_exit_->setText(tr("E&xit"));

    // Record menu actions.
    if (act_rec_start_) act_rec_start_->setText(tr("&Start Recording"));
    if (act_stop_)      act_stop_->setText(tr("&Stop"));
    if (act_playback_)  act_playback_->setText(tr("&Playback"));
    if (act_open_rec_)  act_open_rec_->setText(tr("&Open Recording..."));

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
