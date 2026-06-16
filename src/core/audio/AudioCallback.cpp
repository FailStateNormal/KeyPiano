#include "AudioCallback.h"

#include <chrono>
#include <cstring>

#include "AudioEngine.h"  // CallbackContext, queues, Stats, MidiEvent

namespace keypiano::audio {

int audioCallback(void* outputBuffer, void* /*inputBuffer*/,
                  unsigned int nFrames, double /*streamTime*/,
                  RtAudioStreamStatus status, void* userData) {
  const auto t0 = std::chrono::steady_clock::now();

  auto* ctx = static_cast<CallbackContext*>(userData);
  auto* out = static_cast<float*>(outputBuffer);

  // SynthesizerBase::render() mixes additively, so the buffer must start clean.
  // memset on a fixed-size float buffer touches no allocator — audio-thread safe.
  std::memset(out, 0, sizeof(float) * nFrames * 2);  // interleaved stereo

  if (status & RTAUDIO_OUTPUT_UNDERFLOW) {
    ctx->stats->buffer_underruns.fetch_add(1, std::memory_order_relaxed);
  }

  if (ctx->synth == nullptr) return 0;  // defensive: never happens once started

  // Drain every pending control event into the synth. The queue is bounded
  // (1024), so this loop is finite and wait-free; pop() never spins.
  // Forward note activity to the UI (highlight). Best-effort: if the UI is behind
  // and the feedback queue is full, dropping is fine — just count it so the UI can
  // surface that highlights are lagging. Inlined lambda: no allocation, wait-free.
  auto pushFeedback = [ctx](const MidiEvent& e) {
    if (!ctx->feedback_queue->push(e))
      ctx->stats->feedback_drops.fetch_add(1, std::memory_order_relaxed);
  };

  MidiEvent ev;
  while (ctx->event_queue->pop(ev)) {
    switch (ev.type) {
      case EventType::NoteOn:
        ctx->synth->noteOn(ev.chan, ev.note, ev.vel);
        pushFeedback(ev);
        break;
      case EventType::NoteOff:
        ctx->synth->noteOff(ev.chan, ev.note);
        pushFeedback(ev);
        break;
      case EventType::ControlChange:
        // note/vel carry the CC number/value (see AudioEngine::post).
        ctx->synth->controlChange(ev.chan, ev.note, ev.vel);
        break;
      case EventType::AllNotesOff:
        ctx->synth->allNotesOff();
        pushFeedback(ev);  // let the UI clear all highlights
        break;
    }
  }

  // Mix one buffer of audio.
  ctx->synth->render(out, nFrames);

  // Output-stage master volume. Relaxed load — it's a single scalar with no
  // ordering dependency on anything else here. Skip the multiply at unity so the
  // common (full-volume) case touches nothing. Audio-thread safe: no alloc/lock.
  if (ctx->master_gain) {
    const float gain = ctx->master_gain->load(std::memory_order_relaxed);
    if (gain != 1.0f) {
      const unsigned int n = nFrames * 2;  // interleaved stereo
      for (unsigned int i = 0; i < n; ++i) out[i] *= gain;
    }
  }

  // Publish load/latency metrics. cpu_load is the fraction of the period budget
  // this callback consumed; >1.0 means we are at risk of an underrun.
  const auto t1 = std::chrono::steady_clock::now();
  const double elapsed_us =
      std::chrono::duration<double, std::micro>(t1 - t0).count();
  const double budget_us =
      static_cast<double>(nFrames) / ctx->sample_rate * 1e6;
  if (budget_us > 0.0) {
    ctx->stats->cpu_load.store(elapsed_us / budget_us,
                               std::memory_order_relaxed);
    ctx->stats->latency_us.store(static_cast<uint32_t>(budget_us),
                                 std::memory_order_relaxed);
  }

  return 0;
}

}  // namespace keypiano::audio
