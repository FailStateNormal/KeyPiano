#include "bridge/AudioBridge.h"

#include <QTimer>

#include "audio/MidiEvent.h"

namespace keypiano::ui {

AudioBridge::AudioBridge(audio::AudioEngine* engine, QObject* parent)
    : QObject(parent), engine_(engine) {
    timer_ = new QTimer(this);
    timer_->setInterval(16);
    connect(timer_, &QTimer::timeout, this, &AudioBridge::poll);
}

void AudioBridge::start() { timer_->start(); }
void AudioBridge::stop()  { timer_->stop(); }

void AudioBridge::poll() {
    MidiEvent ev;
    while (engine_->popFeedback(ev)) {
        switch (ev.type) {
            case EventType::NoteOn:
                emit noteActivated(static_cast<int>(ev.note));
                break;
            case EventType::NoteOff:
                emit noteReleased(static_cast<int>(ev.note));
                break;
            case EventType::AllNotesOff:
                emit noteReleased(-1);
                break;
            case EventType::ControlChange:
                break;
        }
    }
}

} // namespace keypiano::ui
