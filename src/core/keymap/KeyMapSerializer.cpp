#include "keymap/KeyMapSerializer.h"

#include <sstream>
#include <unordered_map>

namespace keypiano {

static const std::unordered_map<uint32_t, std::string>& vkNameMap() {
  static const std::unordered_map<uint32_t, std::string> m = {
    {0x41,"A"},{0x42,"B"},{0x43,"C"},{0x44,"D"},{0x45,"E"},{0x46,"F"},
    {0x47,"G"},{0x48,"H"},{0x49,"I"},{0x4A,"J"},{0x4B,"K"},{0x4C,"L"},
    {0x4D,"M"},{0x4E,"N"},{0x4F,"O"},{0x50,"P"},{0x51,"Q"},{0x52,"R"},
    {0x53,"S"},{0x54,"T"},{0x55,"U"},{0x56,"V"},{0x57,"W"},{0x58,"X"},
    {0x59,"Y"},{0x5A,"Z"},
    {0x30,"0"},{0x31,"1"},{0x32,"2"},{0x33,"3"},{0x34,"4"},
    {0x35,"5"},{0x36,"6"},{0x37,"7"},{0x38,"8"},{0x39,"9"},
    {0x70,"F1"},{0x71,"F2"},{0x72,"F3"},{0x73,"F4"},
    {0x74,"F5"},{0x75,"F6"},{0x76,"F7"},{0x77,"F8"},
    {0x78,"F9"},{0x79,"F10"},{0x7A,"F11"},{0x7B,"F12"},
    {0x20,"Space"},{0x09,"Tab"},{0x14,"CapsLock"},
    {0x0D,"Return"},{0x08,"Backspace"},{0x1B,"Escape"},
    {0x2E,"Delete"},{0x2D,"Insert"},{0x24,"Home"},{0x23,"End"},
    {0x21,"PageUp"},{0x22,"PageDown"},
    {0x25,"Left"},{0x27,"Right"},{0x26,"Up"},{0x28,"Down"},
    {0x91,"ScrollLock"},{0x90,"NumLock"},{0x13,"Pause"},
    {0x60,"Numpad0"},{0x61,"Numpad1"},{0x62,"Numpad2"},{0x63,"Numpad3"},
    {0x64,"Numpad4"},{0x65,"Numpad5"},{0x66,"Numpad6"},{0x67,"Numpad7"},
    {0x68,"Numpad8"},{0x69,"Numpad9"},
    {0xBA,"Semicolon"},{0xBB,"Equals"},{0xBC,"Comma"},
    {0xBD,"Minus"},{0xBE,"Period"},{0xBF,"Slash"},
    {0xC0,"Grave"},{0xDB,"LBracket"},{0xDC,"Backslash"},
    {0xDD,"RBracket"},{0xDE,"Quote"},
  };
  return m;
}

std::string KeyMapSerializer::keyName(uint32_t vk_code) {
  const auto& nm = vkNameMap();
  auto it = nm.find(vk_code);
  return (it != nm.end()) ? it->second
                          : ("VK_" + std::to_string(vk_code));
}

// MIDI note 0-127 → "C-1", "C#3", "A4", etc.
static std::string midiNoteToString(uint8_t note) {
  static const char* names[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
  int oct = static_cast<int>(note) / 12 - 1;
  int sem = static_cast<int>(note) % 12;
  return std::string(names[sem]) + std::to_string(oct);
}

std::string KeyMapSerializer::serialize(const KeyMap& map) {
  std::ostringstream out;
  out << "FreePiano 2.1\n";
  if (!map.name.empty()) out << "; " << map.name << "\n";
  out << "\n";

  for (const auto& b : map.bindings()) {
    std::string keyName = KeyMapSerializer::keyName(b.vk_code);
    std::string ch = (b.channel == 0) ? "In_0" : "In_1";

    switch (b.action) {
      case KeyAction::Note:
        out << "Keydown " << keyName << " Note " << ch
            << " " << midiNoteToString(b.midi_note) << "\n";
        break;
      case KeyAction::OctaveInc:
        out << "Keydown " << keyName << " Octave " << ch
            << " Inc " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::OctaveDec:
        out << "Keydown " << keyName << " Octave " << ch
            << " Dec " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::VelocityInc:
        out << "Keydown " << keyName << " Velocity " << ch
            << " Inc " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::VelocityDec:
        out << "Keydown " << keyName << " Velocity " << ch
            << " Dec " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::VelocitySet:
        out << "Keydown " << keyName << " Velocity " << ch
            << " Set " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::KeySignatureInc:
        out << "Keydown " << keyName << " KeySignature " << ch
            << " Inc " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::KeySignatureDec:
        out << "Keydown " << keyName << " KeySignature " << ch
            << " Dec " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::SustainSet:
        out << "Keydown " << keyName << " Sustain " << ch
            << " Set " << static_cast<int>(b.step) << "\n";
        break;
      case KeyAction::SustainPedal:
        out << "Keydown " << keyName << " SustainPedal " << ch << "\n";
        break;
      case KeyAction::SostenutoPedal:
        out << "Keydown " << keyName << " Sostenuto " << ch << "\n";
        break;
      case KeyAction::SoftPedal:
        out << "Keydown " << keyName << " Soft " << ch << "\n";
        break;
      case KeyAction::Record:
        out << "Keydown " << keyName << " Record\n";
        break;
    }

    if (!b.label.empty())
      out << "Label " << keyName << " " << b.label << "\n";
  }
  return out.str();
}

}  // namespace keypiano
