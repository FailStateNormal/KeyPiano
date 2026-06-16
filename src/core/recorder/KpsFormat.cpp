#include "recorder/KpsFormat.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace keypiano {

namespace {

#ifdef _WIN32
// Converts a UTF-8 path to UTF-16 so std::fstream's wchar_t* overload (an MSVC
// extension) opens it correctly. std::fstream(const char*) interprets the path in
// the process ANSI code page (GBK on zh-CN), not UTF-8, so Chinese folders /
// filenames — and Chinese Windows usernames in AppData paths — fail to open.
// Callers pass UTF-8 (from Qt's QString::toStdString()).
std::wstring widenUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
  return w;
}
#endif

std::string trim(const std::string& s) {
  const char* ws = " \t\r\n";
  auto start = s.find_first_not_of(ws);
  if (start == std::string::npos) return {};
  auto end = s.find_last_not_of(ws);
  return s.substr(start, end - start + 1);
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

const char* eventTypeName(EventType t) {
  switch (t) {
    case EventType::NoteOn:         return "NoteOn";
    case EventType::NoteOff:        return "NoteOff";
    case EventType::ControlChange:  return "CC";
    case EventType::AllNotesOff:    return "AllNotesOff";
    default:                        return "Unknown";
  }
}

bool parseEventType(const std::string& s, EventType* out) {
  if (s == "NoteOn")      { *out = EventType::NoteOn;        return true; }
  if (s == "NoteOff")     { *out = EventType::NoteOff;       return true; }
  if (s == "CC")          { *out = EventType::ControlChange;  return true; }
  if (s == "AllNotesOff") { *out = EventType::AllNotesOff;   return true; }
  return false;
}

}  // namespace

// ── Write ─────────────────────────────────────────────────────────────────────

bool KpsFormat::write(const std::string& path,
                      const KpsMeta& meta,
                      const std::vector<MidiEvent>& events,
                      std::string* err) {
#ifdef _WIN32
  std::ofstream f(widenUtf8(path));
#else
  std::ofstream f(path);
#endif
  if (!f.is_open()) {
    if (err) *err = "cannot open for writing: " + path;
    return false;
  }

  f << "# keypiano performance sequence v1\n";
  f << "[meta]\n";
  f << "title = "       << meta.title           << "\n";
  f << "created = "     << meta.created         << "\n";
  f << "duration_us = " << meta.duration_us     << "\n";
  f << "event_count = " << static_cast<int32_t>(events.size()) << "\n";
  f << "[events]\n";
  f << "# ts_us type chan note vel\n";

  for (const auto& e : events) {
    f << e.ts_us                       << " "
      << eventTypeName(e.type)         << " "
      << static_cast<int>(e.chan)      << " "
      << static_cast<int>(e.note)      << " "
      << static_cast<int>(e.vel)       << "\n";
  }

  if (!f.good()) {
    if (err) *err = "write error: " + path;
    return false;
  }
  return true;
}

// ── Read ──────────────────────────────────────────────────────────────────────

bool KpsFormat::read(const std::string& path,
                     KpsMeta* meta,
                     std::vector<MidiEvent>* events,
                     std::string* err) {
#ifdef _WIN32
  std::ifstream f(widenUtf8(path));
#else
  std::ifstream f(path);
#endif
  if (!f.is_open()) {
    if (err) *err = "cannot open: " + path;
    return false;
  }

  *meta   = KpsMeta{};
  events->clear();

  enum class Section { None, Meta, Events };
  Section section = Section::None;
  std::string line;
  int lineno = 0;

  while (std::getline(f, line)) {
    ++lineno;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;

    if (t == "[meta]")   { section = Section::Meta;   continue; }
    if (t == "[events]") { section = Section::Events; continue; }

    if (section == Section::Meta) {
      auto eq = t.find('=');
      if (eq == std::string::npos) continue;
      std::string key = trim(toLower(t.substr(0, eq)));
      std::string val = trim(t.substr(eq + 1));

      if (key == "title") {
        meta->title = val;
      } else if (key == "created") {
        meta->created = val;
      } else if (key == "duration_us") {
        try {
          meta->duration_us = std::stoll(val);
        } catch (...) {
          if (err) *err = "line " + std::to_string(lineno) + ": invalid duration_us";
          return false;
        }
      } else if (key == "event_count") {
        try {
          meta->event_count = static_cast<int32_t>(std::stol(val));
        } catch (...) {
          if (err) *err = "line " + std::to_string(lineno) + ": invalid event_count";
          return false;
        }
      }
      // Unknown meta keys are silently ignored (forward-compatible).

    } else if (section == Section::Events) {
      std::istringstream ss(t);
      int64_t ts_us;
      std::string type_str;
      int chan, note, vel;
      if (!(ss >> ts_us >> type_str >> chan >> note >> vel)) {
        if (err) *err = "line " + std::to_string(lineno) + ": malformed event";
        return false;
      }
      MidiEvent e{};
      if (!parseEventType(type_str, &e.type)) {
        if (err) *err = "line " + std::to_string(lineno) + ": unknown event type: " + type_str;
        return false;
      }
      e.ts_us = ts_us;
      e.chan   = static_cast<uint8_t>(chan);
      e.note   = static_cast<uint8_t>(note);
      e.vel    = static_cast<uint8_t>(vel);
      events->push_back(e);
    }
  }
  return true;
}

}  // namespace keypiano
