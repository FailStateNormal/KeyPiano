#include "AudioEngine.h"

namespace keypiano::audio {

AudioEngine::AudioEngine() : dac_(RtAudio::WINDOWS_WASAPI) {}

AudioEngine::~AudioEngine() {
  close();
}

bool AudioEngine::open(const Config& cfg, SynthesizerBase* synth) {
  if (open_ || synth == nullptr) return false;

  config_ = cfg;

  // Resolve the output device id. RtAudio 6.x device ids are opaque and 0 is
  // "invalid"; getDefaultOutputDevice() returns 0 when there is no output at
  // all.
  unsigned int device_id = 0;
  if (cfg.device_name.empty()) {
    device_id = dac_.getDefaultOutputDevice();
  } else {
    for (unsigned int id : dac_.getDeviceIds()) {
      RtAudio::DeviceInfo info = dac_.getDeviceInfo(id);
      if (info.outputChannels > 0 && info.name == cfg.device_name) {
        device_id = id;
        break;
      }
    }
    if (device_id == 0) device_id = dac_.getDefaultOutputDevice();  // fallback
  }
  if (device_id == 0) return false;

  RtAudio::StreamParameters params;
  params.deviceId = device_id;
  params.nChannels = 2;  // interleaved stereo, matches SynthesizerBase::render
  params.firstChannel = 0;

  RtAudio::StreamOptions opts;
  opts.flags = RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
  opts.streamName = "keypiano";

  // Wire the callback context before the stream can possibly fire. The synth
  // is not yet initialised, but startStream() below is what arms the callback.
  context_.synth = synth;
  context_.event_queue = &event_queue_;
  context_.feedback_queue = &feedback_queue_;
  context_.stats = &stats_;
  context_.sample_rate = cfg.sample_rate;

  unsigned int buffer_frames = cfg.buffer_frames;
  RtAudioErrorType err =
      dac_.openStream(&params, /*inputParameters=*/nullptr, RTAUDIO_FLOAT32,
                      cfg.sample_rate, &buffer_frames, &audioCallback,
                      &context_, &opts);
  if (err != RTAUDIO_NO_ERROR) {
    context_ = CallbackContext{};
    return false;
  }

  // RtAudio may have negotiated a different buffer size; record it and size the
  // synth's scratch buffers to match so render() never overflows them.
  config_.buffer_frames = buffer_frames;
  if (!synth->init(cfg.sample_rate, buffer_frames)) {
    dac_.closeStream();
    context_ = CallbackContext{};
    return false;
  }

  start_time_ = std::chrono::steady_clock::now();

  err = dac_.startStream();
  if (err != RTAUDIO_NO_ERROR) {
    synth->shutdown();
    dac_.closeStream();
    context_ = CallbackContext{};
    return false;
  }

  open_ = true;
  return true;
}

void AudioEngine::close() {
  if (!open_) return;

  if (dac_.isStreamRunning()) dac_.stopStream();
  if (dac_.isStreamOpen()) dac_.closeStream();

  // The audio thread is now stopped, so touching the synth here is safe.
  if (context_.synth) context_.synth->shutdown();

  context_ = CallbackContext{};
  open_ = false;
}

void AudioEngine::post(EventType type, uint8_t ch, uint8_t a, uint8_t b) {
  const auto now = std::chrono::steady_clock::now();
  const MidiEvent ev{
      .type = type,
      .chan = ch,
      .note = a,  // note number, or CC number for ControlChange
      .vel = b,   // velocity, or CC value for ControlChange
      .ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
                   now - start_time_)
                   .count(),
  };
  // Best-effort: a full queue means we are overrunning the audio thread badly;
  // dropping is correct — we must never block the producer.
  event_queue_.push(ev);
}

void AudioEngine::postNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  post(EventType::NoteOn, ch, note, vel);
}

void AudioEngine::postNoteOff(uint8_t ch, uint8_t note) {
  post(EventType::NoteOff, ch, note, 0);
}

void AudioEngine::postControlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  post(EventType::ControlChange, ch, cc, val);
}

void AudioEngine::postAllNotesOff(uint8_t ch) {
  post(EventType::AllNotesOff, ch, 0, 0);
}

}  // namespace keypiano::audio
