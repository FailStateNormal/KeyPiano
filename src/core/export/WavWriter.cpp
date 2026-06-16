#include "export/WavWriter.h"

namespace keypiano::exporter {

namespace {

// Little-endian helpers — WAV is always LE regardless of host endianness.
void put_u32(std::ofstream& f, uint32_t v) {
  char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff),
               char((v >> 24) & 0xff)};
  f.write(b, 4);
}
void put_u16(std::ofstream& f, uint16_t v) {
  char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
  f.write(b, 2);
}

}  // namespace

WavWriter::~WavWriter() { close(); }

bool WavWriter::open(const std::string& path, uint32_t sample_rate,
                     uint16_t channels) {
  if (isOpen() || sample_rate == 0 || channels == 0) return false;

  file_.open(path, std::ios::binary | std::ios::trunc);
  if (!file_) return false;

  sample_rate_ = sample_rate;
  channels_ = channels;
  data_bytes_ = 0;

  constexpr uint16_t kBitsPerSample = 16;
  const uint16_t block_align = channels * (kBitsPerSample / 8);
  const uint32_t byte_rate = sample_rate * block_align;

  // Header with placeholder sizes (0); close() seeks back and backfills them.
  file_.write("RIFF", 4);
  put_u32(file_, 0);  // RIFF chunk size — backfilled
  file_.write("WAVE", 4);

  file_.write("fmt ", 4);
  put_u32(file_, 16);                  // PCM fmt chunk size
  put_u16(file_, 1);                   // audio format = PCM
  put_u16(file_, channels);
  put_u32(file_, sample_rate);
  put_u32(file_, byte_rate);
  put_u16(file_, block_align);
  put_u16(file_, kBitsPerSample);

  file_.write("data", 4);
  put_u32(file_, 0);  // data chunk size — backfilled

  return file_.good();
}

bool WavWriter::writeFrames(const int16_t* interleaved, uint32_t frames) {
  if (!isOpen() || interleaved == nullptr) return false;
  const std::size_t bytes =
      static_cast<std::size_t>(frames) * channels_ * sizeof(int16_t);
  file_.write(reinterpret_cast<const char*>(interleaved),
              static_cast<std::streamsize>(bytes));
  if (!file_.good()) return false;
  data_bytes_ += bytes;
  return true;
}

bool WavWriter::close() {
  if (!isOpen()) return true;  // idempotent

  // Backfill RIFF chunk size (file size minus the 8-byte "RIFF"+size prefix =
  // 36 header bytes + data) and the data chunk size.
  const uint32_t data_size = static_cast<uint32_t>(data_bytes_);
  const uint32_t riff_size = 36 + data_size;

  file_.seekp(4, std::ios::beg);
  put_u32(file_, riff_size);
  file_.seekp(40, std::ios::beg);  // 44-byte header: data size is the last u32
  put_u32(file_, data_size);

  const bool ok = file_.good();
  file_.close();
  return ok;
}

}  // namespace keypiano::exporter
