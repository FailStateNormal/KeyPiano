#include "config/CfgParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace keypiano {

// ── Helpers ───────────────────────────────────────────────────────────────────

std::vector<std::string> CfgParser::splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    // Strip Windows \r if present.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string CfgParser::toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string CfgParser::trim(const std::string& s) {
  const auto* ws = " \t\r\n";
  auto start = s.find_first_not_of(ws);
  if (start == std::string::npos) return {};
  auto end = s.find_last_not_of(ws);
  return s.substr(start, end - start + 1);
}

// ── Main parser ───────────────────────────────────────────────────────────────

CfgParseResult CfgParser::parse(const std::string& text) {
  CfgParseResult result;
  auto lines = splitLines(text);

  for (std::size_t i = 0; i < lines.size(); ++i) {
    std::string line = trim(lines[i]);

    // Skip empty lines and comments.
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;

    auto eq = line.find('=');
    if (eq == std::string::npos) {
      result.errors.push_back("line " + std::to_string(i + 1) +
                               ": missing '=' in: " + line);
      continue;
    }

    std::string key   = trim(toLower(line.substr(0, eq)));
    std::string value = trim(line.substr(eq + 1));

    if (value.empty()) {
      result.errors.push_back("line " + std::to_string(i + 1) +
                               ": empty value for key: " + key);
      continue;
    }

    if (key == "soundfont") {
      result.config.sf2_path = value;
    } else if (key == "keymap") {
      result.config.keymap_path = value;
    } else if (key == "device") {
      result.config.audio_device = value;
    } else if (key == "samplerate") {
      try {
        long v = std::stol(value);
        if (v <= 0 || v > 384000) throw std::out_of_range("samplerate");
        result.config.sample_rate = static_cast<uint32_t>(v);
      } catch (...) {
        result.errors.push_back("line " + std::to_string(i + 1) +
                                 ": invalid SampleRate: " + value);
      }
    } else if (key == "buffer") {
      try {
        long v = std::stol(value);
        if (v <= 0 || v > 65536) throw std::out_of_range("buffer");
        result.config.buffer_frames = static_cast<uint32_t>(v);
      } catch (...) {
        result.errors.push_back("line " + std::to_string(i + 1) +
                                 ": invalid Buffer: " + value);
      }
    }
    // Unknown keys are silently ignored (forward-compatible).
  }

  return result;
}

}  // namespace keypiano
