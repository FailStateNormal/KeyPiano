// test_wav_writer.cpp — verifies the self-contained PCM WAV writer emits a valid
// RIFF/WAVE header and correctly backfills the RIFF + data chunk sizes on close.
// No audio device or SoundFont needed; it just writes a temp file and reads the
// bytes back.

#include "export/WavWriter.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using keypiano::exporter::WavWriter;

namespace {

uint32_t rd_u32(const std::vector<char>& b, size_t off) {
  return uint32_t(uint8_t(b[off])) | (uint32_t(uint8_t(b[off + 1])) << 8) |
         (uint32_t(uint8_t(b[off + 2])) << 16) |
         (uint32_t(uint8_t(b[off + 3])) << 24);
}
uint16_t rd_u16(const std::vector<char>& b, size_t off) {
  return uint16_t(uint8_t(b[off])) | uint16_t(uint8_t(b[off + 1]) << 8);
}
std::string tag(const std::vector<char>& b, size_t off) {
  return std::string(b.begin() + off, b.begin() + off + 4);
}
std::vector<char> readAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
}

}  // namespace

TEST(WavWriter, WritesValidHeaderAndBackfillsSizes) {
  const std::string path = "test_wav_writer_tmp.wav";
  const uint32_t sr = 44100;
  const uint16_t ch = 2;
  const uint32_t frames = 100;

  {
    WavWriter w;
    ASSERT_TRUE(w.open(path, sr, ch));
    std::vector<int16_t> buf(static_cast<size_t>(frames) * ch, 1234);
    ASSERT_TRUE(w.writeFrames(buf.data(), frames));
    ASSERT_TRUE(w.close());
  }

  const auto b = readAll(path);
  const uint32_t data_size = frames * ch * 2;  // 16-bit samples
  ASSERT_EQ(b.size(), 44u + data_size);

  EXPECT_EQ(tag(b, 0), "RIFF");
  EXPECT_EQ(rd_u32(b, 4), 36u + data_size);   // RIFF size = 36 + data
  EXPECT_EQ(tag(b, 8), "WAVE");
  EXPECT_EQ(tag(b, 12), "fmt ");
  EXPECT_EQ(rd_u32(b, 16), 16u);              // PCM fmt chunk size
  EXPECT_EQ(rd_u16(b, 20), 1u);               // format = PCM
  EXPECT_EQ(rd_u16(b, 22), ch);               // channels
  EXPECT_EQ(rd_u32(b, 24), sr);               // sample rate
  EXPECT_EQ(rd_u32(b, 28), sr * ch * 2u);     // byte rate
  EXPECT_EQ(rd_u16(b, 32), ch * 2u);          // block align
  EXPECT_EQ(rd_u16(b, 34), 16u);              // bits per sample
  EXPECT_EQ(tag(b, 36), "data");
  EXPECT_EQ(rd_u32(b, 40), data_size);        // data chunk size

  std::remove(path.c_str());
}

TEST(WavWriter, EmptyFileIsHeaderOnly) {
  const std::string path = "test_wav_writer_empty.wav";
  {
    WavWriter w;
    ASSERT_TRUE(w.open(path, 22050, 1));
    ASSERT_TRUE(w.close());
  }
  const auto b = readAll(path);
  ASSERT_EQ(b.size(), 44u);
  EXPECT_EQ(rd_u32(b, 4), 36u);  // RIFF size with no data
  EXPECT_EQ(rd_u32(b, 40), 0u);  // data size 0
  std::remove(path.c_str());
}

TEST(WavWriter, RejectsDegenerateParams) {
  WavWriter w;
  EXPECT_FALSE(w.open("test_wav_bad.wav", 0, 2));      // zero sample rate
  EXPECT_FALSE(w.open("test_wav_bad.wav", 44100, 0));  // zero channels
  EXPECT_FALSE(w.isOpen());
}
