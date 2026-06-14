#include "FluidSynthEngine.h"

#include <algorithm>
#include <cassert>

namespace keypiano {

FluidSynthEngine::FluidSynthEngine() = default;

FluidSynthEngine::~FluidSynthEngine() {
  shutdown();
}

bool FluidSynthEngine::init(uint32_t sample_rate, uint32_t max_frames) {
  assert(!synth_ && "init() called twice without shutdown()");

  settings_ = new_fluid_settings();
  if (!settings_) return false;

  fluid_settings_setnum(settings_, "synth.sample-rate",
                        static_cast<double>(sample_rate));
  // Disable built-in reverb/chorus to minimize audio-thread CPU usage.
  // These can be re-enabled via controlChange if needed.
  fluid_settings_setint(settings_, "synth.reverb.active", 0);
  fluid_settings_setint(settings_, "synth.chorus.active", 0);

  synth_ = new_fluid_synth(settings_);
  if (!synth_) {
    delete_fluid_settings(settings_);
    settings_ = nullptr;
    return false;
  }

  max_frames_ = max_frames;
  buf_left_.assign(max_frames, 0.0f);
  buf_right_.assign(max_frames, 0.0f);
  return true;
}

void FluidSynthEngine::shutdown() {
  if (synth_) {
    delete_fluid_synth(synth_);
    synth_ = nullptr;
  }
  if (settings_) {
    delete_fluid_settings(settings_);
    settings_ = nullptr;
  }
  sfont_id_ = FLUID_FAILED;
  buf_left_.clear();
  buf_right_.clear();
  max_frames_ = 0;
}

bool FluidSynthEngine::loadInstrument(const std::string& path) {
  if (!synth_) return false;

  if (sfont_id_ != FLUID_FAILED) {
    fluid_synth_sfunload(synth_, static_cast<unsigned int>(sfont_id_), /*reset_presets=*/1);
    sfont_id_ = FLUID_FAILED;
  }

  int id = fluid_synth_sfload(synth_, path.c_str(), /*reset_presets=*/1);
  if (id == FLUID_FAILED) return false;

  sfont_id_ = id;
  return true;
}

void FluidSynthEngine::noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  if (synth_) fluid_synth_noteon(synth_, ch, note, vel);
}

void FluidSynthEngine::noteOff(uint8_t ch, uint8_t note) {
  if (synth_) fluid_synth_noteoff(synth_, ch, note);
}

void FluidSynthEngine::controlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  if (synth_) fluid_synth_cc(synth_, ch, cc, val);
}

void FluidSynthEngine::allNotesOff() {
  // chan = -1 turns off all notes on every MIDI channel.
  if (synth_) fluid_synth_all_notes_off(synth_, -1);
}

void FluidSynthEngine::render(float* out, uint32_t frames) {
  if (!synth_ || frames == 0) return;

  // Clamp to preallocated capacity — never overflow the temp buffers.
  frames = std::min(frames, max_frames_);

  fluid_synth_write_float(synth_, static_cast<int>(frames),
                          buf_left_.data(), 0, 1,
                          buf_right_.data(), 0, 1);

  // Mix interleaved L/R pairs into the output buffer.
  for (uint32_t i = 0; i < frames; ++i) {
    out[i * 2]     += buf_left_[i];
    out[i * 2 + 1] += buf_right_[i];
  }
}

}  // namespace keypiano
