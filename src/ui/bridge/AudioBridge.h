#ifndef KEYPIANO_UI_BRIDGE_AUDIOBRIDGE_H_
#define KEYPIANO_UI_BRIDGE_AUDIOBRIDGE_H_

#include <QObject>

#include "audio/AudioEngine.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace keypiano::ui {

// Polls AudioEngine::feedback_queue on the UI thread every 16 ms and converts
// raw MidiEvent feedback into Qt signals for PianoWidget (P3-C) to connect to.
//
// Threading: all methods must be called from the UI thread. poll() is invoked
// by a QTimer, which guarantees it runs on the thread that owns this object.
class AudioBridge : public QObject {
    Q_OBJECT

public:
    explicit AudioBridge(audio::AudioEngine* engine, QObject* parent = nullptr);

    void start();
    void stop();

signals:
    // Emitted when the audio thread reports a NoteOn event.
    // midi_note: MIDI note number 0-127.
    void noteActivated(int midi_note);

    // Emitted on NoteOff, or midi_note == -1 for AllNotesOff (clear all).
    void noteReleased(int midi_note);

private slots:
    void poll();

private:
    audio::AudioEngine* engine_;
    QTimer*             timer_;
};

} // namespace keypiano::ui

#endif // KEYPIANO_UI_BRIDGE_AUDIOBRIDGE_H_
