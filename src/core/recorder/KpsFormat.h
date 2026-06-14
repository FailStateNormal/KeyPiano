#ifndef KEYPIANO_CORE_RECORDER_KPSFORMAT_H_
#define KEYPIANO_CORE_RECORDER_KPSFORMAT_H_

#include "audio/MidiEvent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace keypiano {

struct KpsMeta {
  std::string title;
  std::string created;     // ISO 8601 UTC, e.g. "2026-06-14T00:00:00Z"
  int64_t     duration_us{0};
  int32_t     event_count{0};  // written as events.size(); read back for integrity checks
};

// Reads and writes the keypiano performance sequence (.kps) format.
//
// File layout (UTF-8 text):
//
//   # keypiano performance sequence v1
//   [meta]
//   title = <string>
//   created = <ISO 8601 UTC>
//   duration_us = <int64>
//   event_count = <int32>
//   [events]
//   # ts_us  type  chan  note  vel
//   <int64> <NoteOn|NoteOff|CC|AllNotesOff> <uint8> <uint8> <uint8>
//
// Timestamps are microseconds from the start of the recording (not epoch).
// '#' lines are comments; blank lines are ignored.
class KpsFormat {
 public:
  // Returns false and sets *err on I/O or format error.
  static bool write(const std::string& path,
                    const KpsMeta& meta,
                    const std::vector<MidiEvent>& events,
                    std::string* err = nullptr);

  static bool read(const std::string& path,
                   KpsMeta* meta,
                   std::vector<MidiEvent>* events,
                   std::string* err = nullptr);
};

}  // namespace keypiano

#endif  // KEYPIANO_CORE_RECORDER_KPSFORMAT_H_
