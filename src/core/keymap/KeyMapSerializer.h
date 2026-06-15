#ifndef KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_
#define KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_

#include "keymap/KeyMap.h"

#include <string>

namespace keypiano {

// Serializes a KeyMap to FreePiano 2.1 text format.
class KeyMapSerializer {
 public:
  static std::string serialize(const KeyMap& map);

  // FreePiano key name for a Win32 virtual-key code (e.g. 0x5A -> "Z").
  // Falls back to "VK_<n>" for codes without a known name.
  static std::string keyName(uint32_t vk_code);
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_
