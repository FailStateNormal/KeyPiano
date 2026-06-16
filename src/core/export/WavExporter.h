#ifndef KEYPIANO_CORE_EXPORT_WAVEXPORTER_H_
#define KEYPIANO_CORE_EXPORT_WAVEXPORTER_H_

#include "audio/MidiEvent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace keypiano::exporter {

struct WavExportOptions {
  uint32_t sample_rate  = 44100;
  float    gain         = 1.0f;  // output multiplier — pass the UI master volume
  float    tail_seconds = 3.0f;  // extra render after the last event for release
};

// Offline-render a recorded MIDI event sequence to a 16-bit PCM WAV file, using a
// throwaway FluidSynth instance loaded with `sf2_path`. Events must be ordered by
// ts_us (the recorder appends them in order; out-of-order stamps are clamped).
// Runs synchronously on the caller's thread — offline rendering is much faster
// than real time. Returns false and sets *err on any failure.
bool exportEventsToWav(const std::vector<MidiEvent>& events,
                       const std::string& sf2_path,
                       const std::string& out_path,
                       const WavExportOptions& opts,
                       std::string* err);

}  // namespace keypiano::exporter

#endif  // KEYPIANO_CORE_EXPORT_WAVEXPORTER_H_
