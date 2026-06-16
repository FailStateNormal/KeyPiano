#ifndef KEYPIANO_CORE_EXPORT_WAVWRITER_H_
#define KEYPIANO_CORE_EXPORT_WAVWRITER_H_

#include <cstdint>
#include <fstream>
#include <string>

namespace keypiano::exporter {

// Minimal RIFF/WAVE writer for 16-bit integer PCM. Deliberately self-contained
// (no libsndfile / Win32 MMIO dependency — see FreePiano's CWaveFile for the
// kind of platform-bound boilerplate we avoid here): open() writes a 44-byte
// header with placeholder sizes, writeFrames() streams interleaved samples, and
// close() seeks back to backfill the RIFF and data chunk sizes.
//
// Not thread-safe; drive it from a single thread.
class WavWriter {
 public:
  WavWriter() = default;
  ~WavWriter();  // closes if still open (sizes are backfilled)

  WavWriter(const WavWriter&) = delete;
  WavWriter& operator=(const WavWriter&) = delete;

  // Open `path` for writing and emit the placeholder header. Returns false if the
  // file can't be opened or the parameters are degenerate.
  bool open(const std::string& path, uint32_t sample_rate, uint16_t channels);

  // Append `frames` sample-frames of interleaved 16-bit PCM (frames * channels
  // samples read from `interleaved`). Returns false if not open or on write error.
  bool writeFrames(const int16_t* interleaved, uint32_t frames);

  // Backfill the chunk sizes and close the file. Returns false on I/O error.
  // Idempotent: a second call (or the destructor after an explicit close) no-ops.
  bool close();

  bool isOpen() const { return file_.is_open(); }

 private:
  std::ofstream file_;
  uint32_t sample_rate_ = 0;
  uint16_t channels_ = 0;
  uint64_t data_bytes_ = 0;  // PCM payload bytes written so far
};

}  // namespace keypiano::exporter

#endif  // KEYPIANO_CORE_EXPORT_WAVWRITER_H_
