#include "keymap/KeyMapParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace keypiano {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string toUpper(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

static int safeInt(const std::string& s, int fallback = 0) {
  try { return std::stoi(s); } catch (...) { return fallback; }
}

// Parse "In_0" / "In_1" (2.1) or "0" / "1" (2.0). Returns -1 on failure.
static int parseChannel(const std::string& tok) {
  std::string u = toUpper(tok);
  if (u == "IN_0" || u == "0") return 0;
  if (u == "IN_1" || u == "1") return 1;
  return -1;
}

// ── VK name → virtual-key code ────────────────────────────────────────────────

uint32_t KeyMapParser::vkFromName(const std::string& nameUpper) {
  static const std::unordered_map<std::string, uint32_t> t = {
    {"A",0x41},{"B",0x42},{"C",0x43},{"D",0x44},{"E",0x45},{"F",0x46},
    {"G",0x47},{"H",0x48},{"I",0x49},{"J",0x4A},{"K",0x4B},{"L",0x4C},
    {"M",0x4D},{"N",0x4E},{"O",0x4F},{"P",0x50},{"Q",0x51},{"R",0x52},
    {"S",0x53},{"T",0x54},{"U",0x55},{"V",0x56},{"W",0x57},{"X",0x58},
    {"Y",0x59},{"Z",0x5A},
    {"0",0x30},{"1",0x31},{"2",0x32},{"3",0x33},{"4",0x34},
    {"5",0x35},{"6",0x36},{"7",0x37},{"8",0x38},{"9",0x39},
    {"F1",0x70},{"F2",0x71},{"F3",0x72},{"F4",0x73},
    {"F5",0x74},{"F6",0x75},{"F7",0x76},{"F8",0x77},
    {"F9",0x78},{"F10",0x79},{"F11",0x7A},{"F12",0x7B},
    {"SPACE",0x20},{"TAB",0x09},{"CAPS",0x14},{"CAPSLOCK",0x14},
    {"ENTER",0x0D},{"RETURN",0x0D},{"BACKSPACE",0x08},{"BACK",0x08},
    {"ESCAPE",0x1B},{"ESC",0x1B},{"DELETE",0x2E},{"DEL",0x2E},
    {"INSERT",0x2D},{"HOME",0x24},{"END",0x23},
    {"PAGEUP",0x21},{"PRIOR",0x21},{"PAGEDOWN",0x22},{"NEXT",0x22},
    {"LEFT",0x25},{"RIGHT",0x27},{"UP",0x26},{"DOWN",0x28},
    {"SCROLLLOCK",0x91},{"SCROLL",0x91},{"NUMLOCK",0x90},
    {"PAUSE",0x13},{"PRINTSCREEN",0x2C},
    {"NUMPAD0",0x60},{"NUMPAD1",0x61},{"NUMPAD2",0x62},{"NUMPAD3",0x63},
    {"NUMPAD4",0x64},{"NUMPAD5",0x65},{"NUMPAD6",0x66},{"NUMPAD7",0x67},
    {"NUMPAD8",0x68},{"NUMPAD9",0x69},
    {"NUMPADMULTIPLY",0x6A},{"NUMPADADD",0x6B},
    {"NUMPADSUBTRACT",0x6D},{"NUMPADDECIMAL",0x6E},{"NUMPADDIVIDE",0x6F},
    {"SEMICOLON",0xBA},{"EQUALS",0xBB},{"COMMA",0xBC},
    {"MINUS",0xBD},{"PERIOD",0xBE},{"SLASH",0xBF},
    {"GRAVE",0xC0},{"TILDE",0xC0},
    {"LBRACKET",0xDB},{"BACKSLASH",0xDC},{"RBRACKET",0xDD},
    {"QUOTE",0xDE},{"APOSTROPHE",0xDE},
  };
  auto it = t.find(nameUpper);
  return (it != t.end()) ? it->second : 0;
}

// ── MIDI note parsing ─────────────────────────────────────────────────────────
// Accepts "c2", "d#3", "eb4" (2.0) and "C4", "Eb3", "F#5" (2.1).
// Octave is relative to MIDI standard: C-1 = 0, C4 = 60.

int KeyMapParser::parseMidiNote(const std::string& token) {
  if (token.size() < 2) return -1;

  char noteChar = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
  if (noteChar < 'A' || noteChar > 'G') return -1;

  static const int base[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  int semitone = base[noteChar - 'A'];

  size_t pos = 1;
  if (pos < token.size() && token[pos] == '#') { semitone += 1; ++pos; }
  else if (pos < token.size() && token[pos] == 'b') { semitone -= 1; ++pos; }

  if (pos >= token.size()) return -1;
  bool neg = (token[pos] == '-');
  if (neg) ++pos;
  if (pos >= token.size() || !std::isdigit(static_cast<unsigned char>(token[pos]))) return -1;

  int oct = 0;
  while (pos < token.size() && std::isdigit(static_cast<unsigned char>(token[pos])))
    oct = oct * 10 + (token[pos++] - '0');
  if (neg) oct = -oct;

  int note = (oct + 1) * 12 + semitone;
  return (note >= 0 && note <= 127) ? note : -1;
}

// ── Line utilities ─────────────────────────────────────────────────────────────

std::vector<std::string> KeyMapParser::splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> KeyMapParser::tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream ss(line);
  std::string tok;
  while (ss >> tok) {
    if (tok[0] == ';' || tok[0] == '#') break;  // comment
    tokens.push_back(tok);
  }
  return tokens;
}

// ── Top-level dispatcher ──────────────────────────────────────────────────────

ParseResult KeyMapParser::parse(const std::string& text) {
  ParseResult out;
  auto lines = splitLines(text);
  if (lines.empty()) { out.errors.push_back("empty file"); return out; }

  std::string first = lines[0];
  // Strip UTF-8 BOM
  if (first.size() >= 3 &&
      static_cast<unsigned char>(first[0]) == 0xEF &&
      static_cast<unsigned char>(first[1]) == 0xBB &&
      static_cast<unsigned char>(first[2]) == 0xBF)
    first = first.substr(3);

  std::string u = toUpper(first);
  if      (u.find("FREEPIANO 2.0") != std::string::npos) parse20(lines, out);
  else if (u.find("FREEPIANO 2.1") != std::string::npos) parse21(lines, out);
  else    out.errors.push_back("unknown format: '" + first + "'");
  return out;
}

// ── 2.0 parser ────────────────────────────────────────────────────────────────

void KeyMapParser::parse20(const std::vector<std::string>& lines, ParseResult& out) {
  auto err = [&](size_t i, const std::string& msg) {
    out.errors.push_back("line " + std::to_string(i + 1) + ": " + msg);
  };

  for (size_t i = 1; i < lines.size(); ++i) {
    auto t = tokenize(lines[i]);
    if (t.empty()) continue;
    std::string kw = toUpper(t[0]);

    // Global initializer lines — no key binding produced
    if (kw == "SUSTAIN" || kw == "VELOCITY" ||
        kw == "KEYBOARDVELOCITY" || kw == "KEYBOARDVEOLCITY") continue;

    if (kw == "LABEL") {
      if (t.size() < 3) { err(i, "label needs keyname and text"); continue; }
      uint32_t vk = vkFromName(toUpper(t[1]));
      if (!vk) { err(i, "unknown key '" + t[1] + "'"); continue; }
      out.map.setLabel(vk, t[2]);
      continue;
    }

    if (kw != "KEY" && kw != "KEYDOWN" && kw != "KEYUP") continue;
    if (t.size() < 3) { err(i, "incomplete binding"); continue; }

    uint32_t vk = vkFromName(toUpper(t[1]));
    if (!vk) { err(i, "unknown key '" + t[1] + "'"); continue; }

    std::string act = toUpper(t[2]);
    KeyBinding b; b.vk_code = vk;

    if (act == "NOTEON" || act == "NOTEOFF" || act == "NOTE") {
      if (t.size() < 5) { err(i, "NoteOn needs channel and note"); continue; }
      int ch = parseChannel(t[3]), note = parseMidiNote(t[4]);
      if (ch < 0 || note < 0) { err(i, "bad channel or note"); continue; }
      b.action = KeyAction::Note;
      b.channel = static_cast<uint8_t>(ch);
      b.midi_note = static_cast<uint8_t>(note);
    } else if (act == "OCTAVE") {
      if (t.size() < 5) { err(i, "Octave needs channel and Inc/Dec"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 1) : 1);
      b.action = (dir == "INC") ? KeyAction::OctaveInc : KeyAction::OctaveDec;
    } else if (act == "VELOCITY" || act == "KEYBOARDVELOCITY" || act == "KEYBOARDVEOLCITY") {
      if (t.size() < 5) { err(i, "Velocity needs channel and Inc/Dec/Set"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      if (dir == "INC") { b.action = KeyAction::VelocityInc; b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 10) : 10); }
      else if (dir == "DEC") { b.action = KeyAction::VelocityDec; b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 10) : 10); }
      else { b.action = KeyAction::VelocitySet; b.step = static_cast<int8_t>(safeInt(t[4], 80)); }
    } else if (act == "KEYSIGNATURE") {
      if (t.size() < 5) { err(i, "KeySignature needs channel and Inc/Dec"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 1) : 1);
      b.action = (dir == "INC") ? KeyAction::KeySignatureInc : KeyAction::KeySignatureDec;
    } else if (act == "SUSTAIN") {
      if (t.size() < 5) { err(i, "Sustain needs channel and Set value"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      b.action = KeyAction::SustainSet;
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 127) : 127);
    } else if (act == "SUSTAINPEDAL" || act == "SOSTENUTO" ||
               act == "SOFT" || act == "SOFTPEDAL") {
      // Momentary pedal: "<key> SustainPedal|Sostenuto|Soft <channel>".
      if (t.size() < 4) { err(i, "pedal needs channel"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      if (act == "SUSTAINPEDAL")      b.action = KeyAction::SustainPedal;
      else if (act == "SOSTENUTO")    b.action = KeyAction::SostenutoPedal;
      else                            b.action = KeyAction::SoftPedal;
    } else if (act == "RECORD") {
      b.action = KeyAction::Record;
    } else {
      err(i, "unknown action '" + t[2] + "'"); continue;
    }
    out.map.addBinding(b);
  }
}

// ── 2.1 parser ────────────────────────────────────────────────────────────────

void KeyMapParser::parse21(const std::vector<std::string>& lines, ParseResult& out) {
  auto err = [&](size_t i, const std::string& msg) {
    out.errors.push_back("line " + std::to_string(i + 1) + ": " + msg);
  };

  for (size_t i = 1; i < lines.size(); ++i) {
    auto t = tokenize(lines[i]);
    if (t.empty()) continue;
    std::string kw = toUpper(t[0]);

    if (kw == "LABEL") {
      if (t.size() < 3) { err(i, "label needs keyname and text"); continue; }
      uint32_t vk = vkFromName(toUpper(t[1]));
      if (!vk) { err(i, "unknown key '" + t[1] + "'"); continue; }
      out.map.setLabel(vk, t[2]);
      continue;
    }

    // Metadata lines — skip
    if (kw == "GROUPCOUNT" || kw == "GROUP" || kw == "KEYSIGNATURE") continue;

    if (kw != "KEYDOWN" && kw != "KEYUP") continue;
    if (t.size() < 3) { err(i, "incomplete binding"); continue; }

    uint32_t vk = vkFromName(toUpper(t[1]));
    if (!vk) { err(i, "unknown key '" + t[1] + "'"); continue; }

    std::string act = toUpper(t[2]);
    KeyBinding b; b.vk_code = vk;

    if (act == "NOTE") {
      if (t.size() < 5) { err(i, "Note needs channel and note"); continue; }
      int ch = parseChannel(t[3]), note = parseMidiNote(t[4]);
      if (ch < 0 || note < 0) { err(i, "bad channel or note"); continue; }
      b.action = KeyAction::Note;
      b.channel = static_cast<uint8_t>(ch);
      b.midi_note = static_cast<uint8_t>(note);
    } else if (act == "OCTAVE") {
      if (t.size() < 5) { err(i, "Octave needs channel and Inc/Dec"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 1) : 1);
      b.action = (dir == "INC") ? KeyAction::OctaveInc : KeyAction::OctaveDec;
    } else if (act == "VELOCITY") {
      if (t.size() < 5) { err(i, "Velocity needs channel and Inc/Dec/Set"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      if (dir == "INC") { b.action = KeyAction::VelocityInc; b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 10) : 10); }
      else if (dir == "DEC") { b.action = KeyAction::VelocityDec; b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 10) : 10); }
      else { b.action = KeyAction::VelocitySet; b.step = static_cast<int8_t>(safeInt(t[4], 80)); }
    } else if (act == "KEYSIGNATURE") {
      if (t.size() < 5) { err(i, "KeySignature needs channel and Inc/Dec/Set"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      std::string dir = toUpper(t[4]);
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 1) : 1);
      b.action = (dir == "INC") ? KeyAction::KeySignatureInc : KeyAction::KeySignatureDec;
    } else if (act == "SUSTAIN") {
      if (t.size() < 5) { err(i, "Sustain needs channel and Set value"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      b.action = KeyAction::SustainSet;
      b.step = static_cast<int8_t>(t.size() >= 6 ? safeInt(t[5], 127) : 127);
    } else if (act == "SUSTAINPEDAL" || act == "SOSTENUTO" ||
               act == "SOFT" || act == "SOFTPEDAL") {
      // Momentary pedal: "<key> SustainPedal|Sostenuto|Soft <channel>".
      if (t.size() < 4) { err(i, "pedal needs channel"); continue; }
      int ch = parseChannel(t[3]);
      if (ch < 0) { err(i, "bad channel"); continue; }
      b.channel = static_cast<uint8_t>(ch);
      if (act == "SUSTAINPEDAL")      b.action = KeyAction::SustainPedal;
      else if (act == "SOSTENUTO")    b.action = KeyAction::SostenutoPedal;
      else                            b.action = KeyAction::SoftPedal;
    } else if (act == "RECORD") {
      b.action = KeyAction::Record;
    } else {
      err(i, "unknown action '" + t[2] + "'"); continue;
    }
    out.map.addBinding(b);
  }
}

}  // namespace keypiano
