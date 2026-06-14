// test_cfg_parser.cpp — unit tests for CfgParser (P1-H)

#include "config/CfgParser.h"

#include <gtest/gtest.h>

using namespace keypiano;

TEST(CfgParser, EmptyText) {
  auto r = CfgParser::parse("");
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.config.sample_rate, 44100u);
  EXPECT_EQ(r.config.buffer_frames, 256u);
}

TEST(CfgParser, CommentsAndBlanks) {
  const char* text = "# This is a comment\n"
                     "; Another comment\n"
                     "\n"
                     "   \n";
  auto r = CfgParser::parse(text);
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(r.config.sf2_path.empty());
}

TEST(CfgParser, BasicKeys) {
  const char* text = "SoundFont = C:/sounds/piano.sf2\n"
                     "KeyMap    = C:/maps/default.map\n"
                     "Device    = WASAPI Default\n"
                     "SampleRate = 48000\n"
                     "Buffer     = 512\n";
  auto r = CfgParser::parse(text);
  EXPECT_TRUE(r.ok()) << r.errors[0];
  EXPECT_EQ(r.config.sf2_path,      "C:/sounds/piano.sf2");
  EXPECT_EQ(r.config.keymap_path,   "C:/maps/default.map");
  EXPECT_EQ(r.config.audio_device,  "WASAPI Default");
  EXPECT_EQ(r.config.sample_rate,   48000u);
  EXPECT_EQ(r.config.buffer_frames, 512u);
}

TEST(CfgParser, CaseInsensitiveKeys) {
  const char* text = "SOUNDFONT = piano.sf2\n"
                     "samplerate = 22050\n";
  auto r = CfgParser::parse(text);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.config.sf2_path, "piano.sf2");
  EXPECT_EQ(r.config.sample_rate, 22050u);
}

TEST(CfgParser, WindowsLineEndings) {
  const char* text = "SoundFont = piano.sf2\r\nBuffer = 128\r\n";
  auto r = CfgParser::parse(text);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.config.sf2_path, "piano.sf2");
  EXPECT_EQ(r.config.buffer_frames, 128u);
}

TEST(CfgParser, UnknownKeysIgnored) {
  const char* text = "SoundFont = piano.sf2\n"
                     "UnknownKey = somevalue\n";
  auto r = CfgParser::parse(text);
  // Unknown keys produce no error (forward-compatible).
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.config.sf2_path, "piano.sf2");
}

TEST(CfgParser, MissingEquals) {
  const char* text = "SoundFont = piano.sf2\n"
                     "bad line without equals\n";
  auto r = CfgParser::parse(text);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.errors.size(), 1u);
  EXPECT_EQ(r.config.sf2_path, "piano.sf2");  // valid line still parsed
}

TEST(CfgParser, InvalidSampleRate) {
  auto r = CfgParser::parse("SampleRate = notanumber\n");
  EXPECT_FALSE(r.ok());
  // Default should be preserved.
  EXPECT_EQ(r.config.sample_rate, 44100u);
}

TEST(CfgParser, InvalidBuffer) {
  auto r = CfgParser::parse("Buffer = -5\n");
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.config.buffer_frames, 256u);
}

TEST(CfgParser, EmptyValue) {
  auto r = CfgParser::parse("SoundFont =   \n");
  EXPECT_FALSE(r.ok());
  EXPECT_TRUE(r.config.sf2_path.empty());
}

TEST(CfgParser, FreePianoStyleCfg) {
  // Mirrors what a real freepiano.cfg looks like.
  const char* text =
      "# FreePiano configuration\n"
      "SoundFont = sf2/GeneralUser.sf2\n"
      "KeyMap    = keymap/freepiano.map\n"
      "SampleRate = 44100\n"
      "Buffer     = 256\n";
  auto r = CfgParser::parse(text);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.config.sf2_path,    "sf2/GeneralUser.sf2");
  EXPECT_EQ(r.config.keymap_path, "keymap/freepiano.map");
  EXPECT_EQ(r.config.sample_rate, 44100u);
  EXPECT_EQ(r.config.buffer_frames, 256u);
}
