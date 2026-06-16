#include "RecordingController.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include "recorder/KpsFormat.h"

namespace keypiano::ui {

RecordingController::RecordingController(QWidget* dialog_parent, QObject* parent)
    : QObject(parent), dialog_parent_(dialog_parent) {}

void RecordingController::setActions(QAction* start, QAction* stop,
                                     QAction* playback) {
    act_start_    = start;
    act_stop_     = stop;
    act_playback_ = playback;
    syncActions();
}

void RecordingController::setRecorder(std::unique_ptr<Recorder> rec) {
    recorder_ = std::move(rec);
    syncActions();
}

void RecordingController::clearRecorder() {
    if (recorder_) {
        recorder_->stopPlayback();
        recorder_->stopRecording();
        recorder_.reset();
    }
    syncActions();
}

void RecordingController::feed(const MidiEvent& ev) {
    if (!recorder_ || recorder_->state() != Recorder::State::Recording) return;
    MidiEvent e = ev;
    e.ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - record_start_)
                  .count();
    recorder_->onMidiEvent(e);
}

void RecordingController::syncActions() {
    const auto state = recorder_ ? recorder_->state() : Recorder::State::Idle;
    const bool idle  = (state == Recorder::State::Idle);
    const bool busy  = (state == Recorder::State::Recording) ||
                       (state == Recorder::State::Playing);

    if (act_start_)    act_start_->setEnabled(idle);
    if (act_stop_)     act_stop_->setEnabled(busy);
    if (act_playback_) act_playback_->setEnabled(idle && recorder_ &&
                                                 recorder_->eventCount() > 0);
}

void RecordingController::startRecording() {
    if (!recorder_) return;
    record_start_ = std::chrono::steady_clock::now();
    recorder_->startRecording();
    syncActions();
    emit status(tr("Recording..."), 0);
}

void RecordingController::stop() {
    if (!recorder_) return;

    if (recorder_->state() == Recorder::State::Playing) {
        recorder_->stopPlayback();
        syncActions();
        emit status(tr("Playback stopped."), 3000);
        return;
    }

    recorder_->stopRecording();
    syncActions();

    const auto n = static_cast<int>(recorder_->eventCount());
    emit status(tr("Recording stopped — %1 events.").arg(n), 0);

    if (n == 0) return;

    auto reply = QMessageBox::question(
        dialog_parent_, tr("Save Recording"),
        tr("Save %1 event(s) to file?").arg(n),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QString path = QFileDialog::getSaveFileName(
        dialog_parent_, tr("Save Recording"), {},
        tr("keypiano performance (*.kps);;All files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".kps", Qt::CaseInsensitive)) path += ".kps";

    KpsMeta meta;
    meta.title = title_.toStdString();
    std::string err;
    if (!recorder_->saveToFile(path.toStdString(), meta, &err)) {
        QMessageBox::critical(dialog_parent_, tr("Save Error"),
                              tr("Failed to save:\n%1")
                                  .arg(QString::fromStdString(err)));
    } else {
        emit status(tr("Saved: %1").arg(QFileInfo(path).fileName()), 5000);
    }
}

void RecordingController::startPlayback() {
    if (!recorder_) return;
    if (!recorder_->startPlayback()) {
        QMessageBox::information(dialog_parent_, tr("Playback"),
                                 tr("Nothing to play back.\n"
                                    "Record something first."));
        return;
    }
    syncActions();
    emit status(tr("Playing back %1 event(s)...")
                    .arg(static_cast<int>(recorder_->eventCount())),
                0);
}

void RecordingController::toggleFromHotkey() {
    if (!recorder_) return;
    if (recorder_->state() == Recorder::State::Recording)
        stop();
    else if (recorder_->state() == Recorder::State::Idle)
        startRecording();
}

}  // namespace keypiano::ui
