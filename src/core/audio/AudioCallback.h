// keypiano — RtAudio callback for the audio thread (see plan/PLAN_DETAIL.txt §3).
//
// This is the single function that runs on the WASAPI audio thread. Its only
// job each period is: drain the event_queue into the synth, render one buffer,
// and forward note activity to the UI via the feedback_queue.
//
// 音频线程铁律 (audio-thread contract): zero malloc/new, zero blocking lock,
// zero Qt calls, zero file I/O. Everything it touches was preallocated when the
// stream was opened. The CallbackContext it operates on lives in AudioEngine.h.

#ifndef KEYPIANO_CORE_AUDIO_AUDIOCALLBACK_H_
#define KEYPIANO_CORE_AUDIO_AUDIOCALLBACK_H_

#include <rtaudio/RtAudio.h>

namespace keypiano::audio {

// Matches the RtAudioCallback typedef. `userData` is a CallbackContext*
// (defined in AudioEngine.h) wired up by AudioEngine::open().
int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nFrames,
                  double streamTime, RtAudioStreamStatus status,
                  void* userData);

}  // namespace keypiano::audio

#endif  // KEYPIANO_CORE_AUDIO_AUDIOCALLBACK_H_
