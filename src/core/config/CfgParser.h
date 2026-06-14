#ifndef KEYPIANO_CORE_CONFIG_CFGPARSER_H_
#define KEYPIANO_CORE_CONFIG_CFGPARSER_H_

#include "config/AppConfig.h"

#include <string>
#include <vector>

namespace keypiano {

struct CfgParseResult {
  AppConfig               config;
  std::vector<std::string> errors;  // non-fatal line-level errors
  bool ok() const { return errors.empty(); }
};

// Parses a FreePiano-compatible .cfg text into an AppConfig.
//
// Supported keys (case-insensitive):
//   SoundFont  = <path>
//   KeyMap     = <path>
//   Device     = <name>
//   SampleRate = <hz>
//   Buffer     = <frames>
//
// Lines starting with '#' or ';' are comments. Unknown keys are silently
// ignored. Malformed value lines are recorded in errors and skipped.
class CfgParser {
 public:
  static CfgParseResult parse(const std::string& text);

 private:
  static std::vector<std::string> splitLines(const std::string& text);
  // Returns lowercase copy of s.
  static std::string toLower(std::string s);
  // Strips leading/trailing whitespace.
  static std::string trim(const std::string& s);
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_CONFIG_CFGPARSER_H_
