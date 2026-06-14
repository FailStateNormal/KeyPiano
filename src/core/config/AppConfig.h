#ifndef KEYPIANO_CORE_CONFIG_APPCONFIG_H_
#define KEYPIANO_CORE_CONFIG_APPCONFIG_H_

#include <cstdint>
#include <string>

namespace keypiano {

struct AppConfig {
  std::string sf2_path;         // path to .sf2 soundfont file
  std::string keymap_path;      // path to .map keymap file
  std::string audio_device;     // empty = system default output
  uint32_t    sample_rate    = 44100;
  uint32_t    buffer_frames  = 256;
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_CONFIG_APPCONFIG_H_
