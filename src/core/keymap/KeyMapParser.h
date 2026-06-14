#ifndef KEYPIANO_CORE_KEYMAP_KEYMAPPARSER_H_
#define KEYPIANO_CORE_KEYMAP_KEYMAPPARSER_H_

#include "keymap/KeyMap.h"

#include <string>
#include <vector>

namespace keypiano {

struct ParseResult {
  KeyMap map;
  std::vector<std::string> errors;  // non-fatal line-level errors
  bool ok() const { return errors.empty(); }
};

// Parses FreePiano 2.0 and 2.1 .map text into a KeyMap.
//
// Version is detected from the first line:
//   "FreePiano 2.0" → 2.0 parser
//   "FreePiano 2.1" → 2.1 parser
//
// Both versions are supported simultaneously; malformed lines are recorded
// in ParseResult::errors and skipped (parser never throws).
class KeyMapParser {
 public:
  static ParseResult parse(const std::string& text);

 private:
  // Internal per-version parsers.
  static void parse20(const std::vector<std::string>& lines, ParseResult& out);
  static void parse21(const std::vector<std::string>& lines, ParseResult& out);

  // Shared helpers.
  static std::vector<std::string> splitLines(const std::string& text);
  static std::vector<std::string> tokenize(const std::string& line);

  // Returns VK_* code for a key name string, 0 if unknown.
  // nameUpper should already be upper-cased by the caller.
  static uint32_t vkFromName(const std::string& nameUpper);

  // Parse a note string into a MIDI note number (0-127).
  // Returns -1 on failure.
  // 2.0 format: "c2", "d#3", "eb4"   (lowercase)
  // 2.1 format: "C4", "Eb3", "F#5"   (mixed case — compared case-insensitively)
  static int parseMidiNote(const std::string& token);
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_KEYMAP_KEYMAPPARSER_H_
