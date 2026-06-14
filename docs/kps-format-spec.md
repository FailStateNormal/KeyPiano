# KPS — keypiano Performance Sequence Format Specification

Version: 1  
Encoding: UTF-8 text, LF or CRLF line endings

## Overview

A `.kps` file stores a recorded MIDI performance as a plain-text file.  
It has a magic comment line, a `[meta]` section, and an `[events]` section.

## File Structure

```
# keypiano performance sequence v1

[meta]
title        = <UTF-8 string, single line>
created      = <ISO 8601 UTC, e.g. 2026-06-14T10:30:00Z>
duration_us  = <int64, total duration in microseconds>
event_count  = <int32, number of event lines; use for integrity checks>

[events]
# ts_us  type  chan  note  vel
<ts_us> <type> <chan> <note> <vel>
...
```

## General Rules

- Lines starting with `#` are comments and are ignored.
- Blank lines are ignored.
- Section headers `[meta]` and `[events]` are case-sensitive exact matches.
- Unknown meta keys are silently ignored (forward-compatible design).
- Key matching in `[meta]` is case-insensitive.
- Meta key–value pairs split on the **first** `=`; both sides are stripped of
  leading/trailing whitespace.  A `=` inside a value (e.g. in `title`) is
  preserved verbatim.

## [meta] Fields

| Key           | Type   | Description                                         |
|---------------|--------|-----------------------------------------------------|
| `title`       | string | Human-readable name for the recording               |
| `created`     | string | ISO 8601 UTC creation time                          |
| `duration_us` | int64  | Total recording duration in microseconds            |
| `event_count` | int32  | Number of event lines (written as `events.size()`)  |

## [events] Fields

Each event occupies exactly one line with five space-separated fields:

| Field    | Type   | Range   | Description                                     |
|----------|--------|---------|-------------------------------------------------|
| `ts_us`  | int64  | ≥ 0     | Timestamp in microseconds from recording start  |
| `type`   | string | —       | One of: `NoteOn`, `NoteOff`, `CC`, `AllNotesOff` |
| `chan`   | uint8  | 0–15    | MIDI channel                                    |
| `note`   | uint8  | 0–127   | MIDI note number (or controller number for CC)  |
| `vel`    | uint8  | 0–127   | Velocity / controller value; 0 for NoteOff / AllNotesOff |

### Event Type Mapping

| Token         | C++ EventType               |
|---------------|-----------------------------|
| `NoteOn`      | `EventType::NoteOn`         |
| `NoteOff`     | `EventType::NoteOff`        |
| `CC`          | `EventType::ControlChange`  |
| `AllNotesOff` | `EventType::AllNotesOff`    |

## Example

```
# keypiano performance sequence v1
[meta]
title = Twinkle Twinkle Little Star
created = 2026-06-14T10:30:00Z
duration_us = 6000000
event_count = 8
[events]
# ts_us type chan note vel
0       NoteOn  0 60 80
500000  NoteOff 0 60 0
500000  NoteOn  0 60 80
1000000 NoteOff 0 60 0
1000000 NoteOn  0 67 80
1500000 NoteOff 0 67 0
1500000 NoteOn  0 67 80
2000000 NoteOff 0 67 0
```

## Version History

| Version | Notes            |
|---------|------------------|
| 1       | Initial format   |
