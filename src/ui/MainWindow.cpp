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
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QSlider>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "HelpContent.h"
#include "I18n.h"
#include "KeymapController.h"
#include "RecordingController.h"
#include "SynthController.h"
#include "bridge/AudioBridge.h"
#include "widgets/PianoWidget.h"
#include "widgets/KeyboardOverlayWidget.h"
#include "widgets/PedalIndicatorWidget.h"
#include "dialogs/SoundFontDialog.h"
#include "dialogs/SettingsDialog.h"
#include "recorder/Recorder.h"

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

    synth_ctl_ = new SynthController(this);
    connect(synth_ctl_, &SynthController::status, this,
            [this](const QString& msg, int timeout) {
                if (msg.isEmpty()) statusBar()->clearMessage();
                else               statusBar()->showMessage(msg, timeout);
            });

    setupMenus();
    setupToolBar();
    setupHelpMenu();
    setupStatusBar();
    // Build the piano/pedal widgets BEFORE starting the engine: the engine's
    // afterEngineStart callback connects the audio bridge → piano signals, so the
    // widgets must already exist when the engine first comes up.
    setupPianoWidget();

    // Wire the controller's coordination callbacks. These run at the exact points
    // the old stopEngine()/startEngine() did, preserving the hook/recorder
    // ordering invariants (see SynthController.h).
    SynthController::Host host;
    host.beforeEngineStop = [this] {
        // Uninstall the hook FIRST (joins its thread) so no callback runs while we
        // release the recorder; then drop the recorder before the engine closes.
        if (hook_) { hook_->uninstall(); hook_.reset(); }
        rec_ctl_->clearRecorder();
    };
    host.afterEngineStart = [this](audio::AudioEngine* engine, AudioBridge* bridge) {
        if (engine) {
            rec_ctl_->setRecorder(std::make_unique<Recorder>(
                Recorder::makeAudioDispatch(engine)));
            installHook();
        } else {
            rec_ctl_->setRecorder(
                std::make_unique<Recorder>([](const MidiEvent&) {}));
        }
        // The new synth starts with no pedals down — clear our state + lamps.
        static const int cc[3] = {64, 66, 67};
        for (int i = 0; i < 3; ++i) {
            pedal_engaged_[i].store(false, std::memory_order_relaxed);
            if (pedal_widget_) pedal_widget_->setPedalState(cc[i], false);
        }
        // Reconnect the bridge → PianoWidget signals: the bridge is a fresh object
        // each (re)start, so this targets a new sender (no duplication). The
        // mouse-click path is wired once in setupPianoWidget and is NOT touched
        // here. On the very first start the widgets already exist (setupPianoWidget
        // ran above), so the initial connection is made here too.
        if (piano_widget_ && bridge) {
            connect(bridge, &AudioBridge::noteActivated,
                    piano_widget_, &PianoWidget::onNoteActivated);
            connect(bridge, &AudioBridge::noteReleased,
                    piano_widget_, &PianoWidget::onNoteReleased);
        }
        // A freshly built AudioEngine starts at unity gain — re-apply the user's
        // chosen master volume (same pattern as pedal state above).
        applyMasterVolume();
    };
    host.onInstrumentChanged = [this] { refreshSf2Label(); updateEditorAction(); };
    host.dialogParent = this;
    synth_ctl_->attach(std::move(host));

    audio::AudioEngine::Config cfg;  // defaults: 44100 Hz, 256 frames
    if (!synth_ctl_->initialize(cfg)) {
        QMessageBox::warning(this, tr("Audio Error"),
                             tr("Failed to open audio output device.\n"
                                "SF2 playback will be unavailable."));
    }

    keymap_ctl_->loadStartup();
    synth_ctl_->loadDefault();
    loadPedalModeSetting();
    loadAudioPrefs();       // re-apply volume + background-play from a previous run
    loadLanguageSetting();  // re-apply the language chosen in a previous session

    status_timer_ = new QTimer(this);
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    status_timer_->start(500);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event) {
    status_timer_->stop();
    synth_ctl_->closePluginEditor();  // detach plug-in view while synth is alive
    if (piano_widget_) piano_widget_->releaseAll();
    // shutdown() runs beforeEngineStop (uninstall hook + release recorder, in that
    // order) then stops the bridge and closes the engine — the same ordered
    // teardown as before, now owned by the controller.
    synth_ctl_->shutdown();
    QMainWindow::closeEvent(event);
}

// ── Setup ──────────────────────────────────────────────────────────────────────

void MainWindow::setupMenus() {
    // No Alt-mnemonic letters on the top-level menus: every letter (F/R/H, etc.)
    // can be remapped as a piano key, and the global hook forwards keys to Qt, so
    // a letter mnemonic risks opening a menu mid-performance. The menus stay
    // mouse-accessible and their useful actions all have Ctrl shortcuts.
    file_menu_ = menuBar()->addMenu(tr("File"));

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

    setupAudioMenu();  // Audio menu sits between File and Record

    rec_menu_ = menuBar()->addMenu(tr("Record"));

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

void MainWindow::setupAudioMenu() {
    audio_menu_ = menuBar()->addMenu(tr("Audio"));

    // Master volume: a labelled slider embedded straight in the menu via
    // QWidgetAction. The menu stays open while dragging, so it reads like the
    // little volume popups users already expect.
    auto* vol_widget = new QWidget(this);
    auto* vlay = new QHBoxLayout(vol_widget);
    vlay->setContentsMargins(12, 4, 12, 4);
    vlay->setSpacing(8);
    lbl_volume_ = new QLabel(tr("Volume"), vol_widget);
    volume_slider_ = new QSlider(Qt::Horizontal, vol_widget);
    volume_slider_->setRange(0, 100);
    volume_slider_->setMinimumWidth(140);
    // Set BEFORE connecting so this fires nothing; loadAudioPrefs() later
    // overrides it with the saved value (also without re-persisting).
    volume_slider_->setValue(100);
    lbl_volume_pct_ = new QLabel(QStringLiteral("100%"), vol_widget);
    lbl_volume_pct_->setMinimumWidth(36);
    vlay->addWidget(lbl_volume_);
    vlay->addWidget(volume_slider_, /*stretch=*/1);
    vlay->addWidget(lbl_volume_pct_);

    auto* vol_action = new QWidgetAction(this);
    vol_action->setDefaultWidget(vol_widget);
    audio_menu_->addAction(vol_action);

    connect(volume_slider_, &QSlider::valueChanged,
            this, &MainWindow::onVolumeChanged);

    audio_menu_->addSeparator();

    // Background play: the global hook is always installed; this only gates
    // whether handleKeyboardEvent acts on keys while our window is unfocused.
    // Default off — loadAudioPrefs() applies the saved choice.
    act_background_play_ =
        new QAction(tr("Background Play (map keys when not focused)"), this);
    act_background_play_->setCheckable(true);
    connect(act_background_play_, &QAction::toggled,
            this, &MainWindow::setBackgroundPlay);
    audio_menu_->addAction(act_background_play_);
}

void MainWindow::onVolumeChanged(int percent) {
    if (lbl_volume_pct_)
        lbl_volume_pct_->setText(QStringLiteral("%1%").arg(percent));
    applyMasterVolume();
    QSettings("keypiano", "keypiano").setValue("master_volume", percent);
}

void MainWindow::applyMasterVolume() {
    if (!volume_slider_) return;
    // Map 0..100% linearly onto 0..kMaxMasterGain, so the slider's full scale is
    // an amplifying boost (the bare SF2 output is quiet — see kMaxMasterGain).
    const float gain = static_cast<float>(volume_slider_->value()) / 100.0f
                       * audio::kMaxMasterGain;
    if (auto* engine = synth_ctl_->engine()) engine->setMasterGain(gain);
}

void MainWindow::setBackgroundPlay(bool on) {
    background_play_.store(on, std::memory_order_relaxed);
    if (act_background_play_) act_background_play_->setChecked(on);
    QSettings("keypiano", "keypiano").setValue("background_play", on);
}

void MainWindow::loadAudioPrefs() {
    QSettings settings("keypiano", "keypiano");

    int vol = settings.value("master_volume", 100).toInt();
    vol = vol < 0 ? 0 : vol > 100 ? 100 : vol;
    if (volume_slider_) {
        // Apply without re-persisting (the blocker stops onVolumeChanged firing);
        // then sync the readout label and push the gain to the engine by hand.
        const QSignalBlocker block(volume_slider_);
        volume_slider_->setValue(vol);
    }
    if (lbl_volume_pct_)
        lbl_volume_pct_->setText(QStringLiteral("%1%").arg(vol));
    applyMasterVolume();

    setBackgroundPlay(settings.value("background_play", false).toBool());
}

void MainWindow::panicAllNotes() {
    if (auto* engine = synth_ctl_->engine()) engine->postAllNotesOff(0);
    if (piano_widget_) piano_widget_->releaseAll();
}

void MainWindow::setupHelpMenu() {
    // Language submenu (mutually-exclusive radio items). English/简体中文 are
    // intentionally NOT translated — each is shown in its own language.
    help_menu_ = menuBar()->addMenu(tr("Help"));

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
    // Background-play gate: when off, ignore keys unless our window is focused.
    // Runs on the hook thread, so read both flags as plain atomics — no Qt calls.
    // (On focus loss with background play off, changeEvent() also silences any
    // sounding notes, so a note whose key-up we now drop can't hang.)
    if (!background_play_.load(std::memory_order_relaxed) &&
        !window_active_.load(std::memory_order_relaxed))
        return;

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
        if (engage &&
            synth_ctl_->currentBackend() == SynthController::Backend::Vst3 &&
            (idx == 0 || idx == 1)) {
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

    // The engine is only reassigned by SynthController's stop/start path, which
    // uninstalls the hook first — so it cannot be swapped out from under this
    // callback (the hook thread is joined before engine_ changes).
    auto* engine = synth_ctl_->engine();
    if (!engine) return;
    switch (ev.type) {
        case EventType::NoteOn:
            engine->postNoteOn(ev.chan, ev.note, ev.vel);
            break;
        case EventType::NoteOff:
            engine->postNoteOff(ev.chan, ev.note);
            break;
        case EventType::ControlChange:
            engine->postControlChange(ev.chan, ev.note, ev.vel);
            break;
        case EventType::AllNotesOff:
            engine->postAllNotesOff(ev.chan);
            break;
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

    // The bridge → PianoWidget connection is made by SynthController's
    // afterEngineStart callback (the bridge is recreated on every engine start).

    // Mouse-click playback is wired exactly once, here — never on engine restart,
    // which would stack duplicate connections. The lambdas read the engine live so
    // they follow restarts, and no-op while it is down (e.g. if it failed to open
    // at startup). They also feed the recorder (like the keyboard path) so on-screen
    // clicks are captured in a recording, not just the audio output.
    connect(piano_widget_, &PianoWidget::mouseNoteOn,
            this, [this](int midi, int vel) {
                const auto note = static_cast<uint8_t>(midi);
                const auto v    = softVelocity(vel);  // soft pedal applies to clicks too
                rec_ctl_->feed(MidiEvent{EventType::NoteOn, 0, note, v, 0});
                if (auto* engine = synth_ctl_->engine())
                    engine->postNoteOn(0, note, v);
            });
    connect(piano_widget_, &PianoWidget::mouseNoteOff,
            this, [this](int midi) {
                const auto note = static_cast<uint8_t>(midi);
                rec_ctl_->feed(MidiEvent{EventType::NoteOff, 0, note, 0, 0});
                if (auto* engine = synth_ctl_->engine())
                    engine->postNoteOff(0, note);
            });

    connect(piano_widget_, &PianoWidget::keyClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindKeyClicked);
    connect(pedal_widget_, &PedalIndicatorWidget::pedalClickedForRebind,
            keymap_ctl_, &KeymapController::onRebindPedalClicked);

    overlay_ = new KeyboardOverlayWidget(piano_widget_);

    // The controller drives the overlay + pedal lamps + selection highlight.
    keymap_ctl_->setWidgets(piano_widget_, overlay_, pedal_widget_);
}

// Rebuilds the status-bar instrument label from SynthController's state. Kept in
// MainWindow because it owns the label + the recorder title; called on every
// instrument change (via the onInstrumentChanged host callback) and on language
// switch (to refresh the " (built-in)" suffix).
void MainWindow::refreshSf2Label() {
    const QString name = synth_ctl_->currentName();
    // Keep the recorder's save-file [meta] title in sync with the instrument.
    if (rec_ctl_) rec_ctl_->setRecordingTitle(name);

    if (!lbl_sf2_name_) return;
    const QString prefix =
        (synth_ctl_->currentBackend() == SynthController::Backend::Vst3) ? "VST3: "
                                                                         : "SF2: ";
    QString text = prefix + name;
    if (synth_ctl_->isBuiltin()) text += tr(" (built-in)");
    lbl_sf2_name_->setText(text);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onOpenSf2() {
    SoundFontDialog dlg(this);
    // Pin the bundled GeneralUser GS piano so the user can always pick it back,
    // even though it's never stored in the recent list.
    if (const QString bundled = synth_ctl_->bundledDefaultSf2Path(); !bundled.isEmpty())
        dlg.setBuiltinDefault(bundled, QStringLiteral("GeneralUser-GS.sf2"));
    dlg.setInitialPath(synth_ctl_->currentSf2Path());  // path, or empty for built-in
    if (dlg.exec() != QDialog::Accepted) return;

    const QString path = dlg.selectedPath();
    if (path.isEmpty()) return;

    QString err;
    if (!synth_ctl_->loadSf2(path, &err))
        QMessageBox::critical(this, tr("Error"), err);
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

    QString err;
    if (synth_ctl_->loadVst3(path, &err)) {
        // Remember the bundle's parent folder so the dialog reopens there.
        QSettings("keypiano", "keypiano")
            .setValue(kVst3DirKey, QFileInfo(path).absolutePath());
        return;
    }
    QMessageBox::critical(this, tr("Error"), err);
#endif
}

void MainWindow::onShowPluginEditor() {
    synth_ctl_->showPluginEditor(this);
}

void MainWindow::updateEditorAction() {
    if (!act_show_editor_) return;
    act_show_editor_->setEnabled(synth_ctl_->hasPluginEditor());
}

void MainWindow::onOpenSettings() {
    const auto& cur = synth_ctl_->config();
    SettingsDialog dlg(cur, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto new_cfg = dlg.config();
    if (new_cfg.device_name  == cur.device_name  &&
        new_cfg.sample_rate  == cur.sample_rate   &&
        new_cfg.buffer_frames == cur.buffer_frames)
        return;
    synth_ctl_->restart(new_cfg);
    if (!synth_ctl_->engine()) {
        QMessageBox::critical(this, tr("Audio Error"),
                              tr("Failed to open audio with the new settings.\n"
                                 "Audio is stopped — reopen it from Settings."));
        return;
    }
    statusBar()->showMessage(tr("Audio settings applied."), 3000);
}

void MainWindow::updateStatus() {
    if (auto* engine = synth_ctl_->engine(); engine && engine->isOpen()) {
        const auto& s = engine->stats();
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
    if (event->type() == QEvent::ActivationChange) {
        const bool active = isActiveWindow();
        window_active_.store(active, std::memory_order_relaxed);
        // Losing focus while background play is off: silence any sounding notes,
        // since their key-ups would now be dropped by the gate and hang.
        if (!active && !background_play_.load(std::memory_order_relaxed))
            panicAllNotes();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    // Seed the focus flag before the first ActivationChange arrives, so keys map
    // immediately on a normal foreground launch (background play default off).
    window_active_.store(isActiveWindow(), std::memory_order_relaxed);
    QMainWindow::showEvent(event);
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
            if (auto* engine = synth_ctl_->engine())
                engine->postControlChange(0, static_cast<uint8_t>(cc[i]), 0);
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
    if (file_menu_)   file_menu_->setTitle(tr("File"));
    if (audio_menu_)  audio_menu_->setTitle(tr("Audio"));
    if (rec_menu_)    rec_menu_->setTitle(tr("Record"));
    if (help_menu_)   help_menu_->setTitle(tr("Help"));
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

    // Audio menu.
    if (lbl_volume_) lbl_volume_->setText(tr("Volume"));
    if (act_background_play_)
        act_background_play_->setText(
            tr("Background Play (map keys when not focused)"));

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
