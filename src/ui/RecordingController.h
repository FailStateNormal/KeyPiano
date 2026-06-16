#ifndef KEYPIANO_UI_RECORDINGCONTROLLER_H_
#define KEYPIANO_UI_RECORDINGCONTROLLER_H_

#include <chrono>
#include <memory>

#include <QObject>
#include <QString>

#include "recorder/Recorder.h"

QT_BEGIN_NAMESPACE
class QAction;
class QWidget;
QT_END_NAMESPACE

namespace keypiano::ui {

// Owns the Recorder and everything about recording / playback: the Record menu
// actions' enabled state, the start/stop/playback flow (including the save
// dialog), the hotkey toggle, and feeding live events from the keyboard-hook
// thread. Extracted from MainWindow so the window stays a thin assembler.
//
// Threading: lives on the UI thread. The only hook-thread entry point is feed(),
// which reads the Recorder. MainWindow guarantees the keyboard hook is
// uninstalled (its thread joined) before it calls setRecorder()/clearRecorder(),
// so feed() never races the recorder pointer being swapped or reset — the exact
// invariant the inline code relied on before the extraction.
class RecordingController : public QObject {
    Q_OBJECT

public:
    // `dialog_parent` parents the save / message dialogs.
    explicit RecordingController(QWidget* dialog_parent, QObject* parent = nullptr);

    // One-time wiring once the Record/Stop/Playback actions exist.
    void setActions(QAction* start, QAction* stop, QAction* playback);

    // ── Recorder lifecycle (UI thread) ──────────────────────────────────────
    // MainWindow builds the Recorder (it owns the engine the dispatch routes to)
    // and hands ownership here. clearRecorder() stops playback/recording and
    // releases it. Both MUST be called while the keyboard hook is uninstalled.
    void setRecorder(std::unique_ptr<Recorder> rec);
    void clearRecorder();

    // KPS [meta] title used when saving (the current instrument name).
    void setRecordingTitle(const QString& title) { title_ = title; }

    // ── Hook-thread API (thread-safe w.r.t. the lifecycle invariant above) ───
    // Append a live event if currently recording, timestamping it relative to
    // the recording start. No-op when not recording.
    void feed(const MidiEvent& ev);

    // Refresh the enabled state of the Record / Stop / Playback actions.
    void syncActions();

public slots:
    void startRecording();
    void stop();
    void startPlayback();
    // Record hotkey: start if Idle, stop if Recording (ignored while Playing).
    void toggleFromHotkey();

signals:
    // Show a status-bar message. timeout_ms == 0 means persistent (until the next
    // message). MainWindow forwards this to its status bar.
    void status(const QString& msg, int timeout_ms);

private:
    QWidget* dialog_parent_ = nullptr;
    QAction* act_start_     = nullptr;
    QAction* act_stop_      = nullptr;
    QAction* act_playback_  = nullptr;

    std::unique_ptr<Recorder> recorder_;
    // Set just before recorder_->startRecording() so the hook thread can compute
    // ts_us for each captured event (happens-before via the recorder's atomic
    // state_: feed() reads state_ acquire after this write's release).
    std::chrono::steady_clock::time_point record_start_{};
    QString title_;
};

}  // namespace keypiano::ui

#endif  // KEYPIANO_UI_RECORDINGCONTROLLER_H_
