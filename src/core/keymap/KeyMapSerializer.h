#ifndef KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_
#define KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_

#include "keymap/KeyMap.h"

#include <string>

namespace keypiano {

// Serializes a KeyMap to FreePiano 2.1 text format.
class KeyMapSerializer {
 public:
  static std::string serialize(const KeyMap& map);
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_KEYMAP_KEYMAPSERIALIZER_H_
