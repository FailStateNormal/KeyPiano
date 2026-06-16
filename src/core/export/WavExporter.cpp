#include "export/WavExporter.h"

#include "export/WavWriter.h"
#include "synth/FluidSynthEngine.h"

#include <algorithm>
#include <vector>

namespace keypiano::exporter {

namespace {

int16_t toPcm(float v) {
  v *= 32767.0f;
  if (v > 32767.0f) v = 32767.0f;
  else if (v < -32768.0f) v = -32768.0f;
  return static_cast<int16_t>(v);
}

}  // namespace

bool exportEventsToWav(const std::vector<MidiEvent>& events,
                       const std::string& sf2_path, const std::string& out_path,
                       const WavExportOptions& opts, std::string* err) {
  auto fail = [&](const char* m) {
    if (err) *err = m;
    return false;
  };

  if (events.empty()) return fail("Nothing to export.");
  if (opts.sample_rate == 0) return fail("Invalid sample rate.");

  constexpr uint32_t kBlock = 1024;

  // Throwaway synth dedicated to this export — never touches the live audio
  // engine's synth (which the audio callback owns). RAII: shut down on scope exit.
  FluidSynthEngine synth;
  if (!synth.init(opts.sample_rate, kBlock))
    return fail("Failed to initialize the synthesizer for export.");
  if (!synth.loadInstrument(sf2_path))
    return fail("Failed to load the SoundFont for export.");

  WavWriter wav;
  if (!wav.open(out_path, opts.sample_rate, 2))
    return fail("Failed to open the output WAV file for writing.");

  std::vector<float>   fbuf(static_cast<std::size_t>(kBlock) * 2);
  std::vector<int16_t> pcm(static_cast<std::size_t>(kBlock) * 2);

  // Render `n` sample-frames in kBlock chunks, scaling by gain into 16-bit PCM.
  auto render_samples = [&](int64_t n) -> bool {
    while (n > 0) {
      const uint32_t frames =
          static_cast<uint32_t>(std::min<int64_t>(n, kBlock));
      std::fill(fbuf.begin(), fbuf.begin() + static_cast<std::ptrdiff_t>(frames) * 2,
                0.0f);
      synth.render(fbuf.data(), frames);  // additive — buffer pre-zeroed above
      for (uint32_t i = 0; i < frames * 2; ++i) pcm[i] = toPcm(fbuf[i] * opts.gain);
      if (!wav.writeFrames(pcm.data(), frames)) return false;
      n -= frames;
    }
    return true;
  };

  auto apply = [&](const MidiEvent& e) {
    switch (e.type) {
      case EventType::NoteOn:        synth.noteOn(e.chan, e.note, e.vel); break;
      case EventType::NoteOff:       synth.noteOff(e.chan, e.note); break;
      case EventType::ControlChange: synth.controlChange(e.chan, e.note, e.vel); break;
      case EventType::AllNotesOff:   synth.allNotesOff(); break;
    }
  };

  int64_t pos = 0;  // sample-frames rendered so far
  const auto sr = static_cast<int64_t>(opts.sample_rate);
  for (const MidiEvent& e : events) {
    int64_t target = e.ts_us * sr / 1000000;
    if (target < pos) target = pos;  // clamp any out-of-order timestamp
    if (!render_samples(target - pos)) return fail("Write error during export.");
    pos = target;
    apply(e);
  }

  const auto tail =
      static_cast<int64_t>(opts.tail_seconds * static_cast<float>(sr));
  if (!render_samples(tail)) return fail("Write error during export.");

  if (!wav.close()) return fail("Write error finalizing the WAV file.");
  return true;
}

}  // namespace keypiano::exporter
