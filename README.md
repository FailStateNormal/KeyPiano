# keypiano

A modern Windows virtual piano, rewritten from scratch in C++20. It turns your
computer keyboard into a playable instrument with low-latency SF2 (SoundFont)
synthesis, performance recording/playback, a Qt6 GUI, and VST3 instrument
hosting.

keypiano takes inspiration from [FreePiano](https://github.com/freepiano/freepiano)
(notably its `.map` keyboard-layout format, which keypiano parses for
compatibility) but shares no code with it. It is an independent, GPL v3 project.

## Status

Feature-complete. All four planned phases — plus a round of usability and
robustness work — are done. The audio engine, recording, Qt6 GUI, VST3 hosting,
and packaging all ship.

| Phase | Goal | State |
|-------|------|-------|
| 1 | Core engine — keys make sound, latency < 20 ms, no GUI | ✅ complete (~5.8 ms latency) |
| 2 | Recording & playback (`.kps` format) | ✅ complete |
| 3 | Qt6 Widgets GUI — visual piano | ✅ complete |
| 4 | VST3 instrument hosting + packaging | ✅ complete |

**96/96** unit tests pass; release builds produce a self-contained, ready-to-zip
folder. Grab a build from the [Releases](../../releases) page, or build it
yourself (below).

## Features

- **Plug-and-play** — ships with a real sampled grand piano (GeneralUser GS), so
  it makes sound the moment you launch it; no setup required.
- **Low latency** — RtAudio over WASAPI, lock-free event queues (~5.8 ms).
- **SoundFont synthesis** — FluidSynth 2.3+; load any `.sf2`, with a recent-files
  picker and a one-click "built-in default" entry.
- **VST3 instruments** — host `.vst3` instrument plug-ins and, when the plug-in
  provides one, its native editor GUI. No VST SDK download required (pulled via
  vcpkg). A failed load rolls back to the previous working instrument.
- **Three pedals** — sustain, sostenuto and soft (una corda), in Hold or Toggle
  mode, with on-screen indicator lamps.
- **Remappable keyboard** — click a key and press a physical key to rebind it;
  clear bindings; save/switch named layouts (presets). Parses both FreePiano 2.0
  and 2.1 `.map` syntax.
- **Recording** — capture a performance to a readable, Git-friendly `.kps` text
  file with microsecond precision; replay it, or open and play back any saved
  `.kps` later.
- **Bilingual UI** — switch between English and 简体中文 at any time; full Unicode
  (Chinese) file-path support.

## Usage

Launch it and start playing — two rows of keys span two octaves (`Z X C V B N M`
lower, `Y U I O P [ ]` upper; the home/number rows are the black keys). `F1`/`F2`
shift the octave, `Space`/`F3`/`F4` are the sustain/sostenuto/soft pedals. You can
also click the on-screen keys with the mouse.

Common shortcuts: `Ctrl+O` open SF2, `Ctrl+Shift+O` open VST3, `Ctrl+R` record,
`Ctrl+.` stop, `Ctrl+P` playback, `Ctrl+Shift+P` open a saved recording. The full
list and a walkthrough live under **Help → Usage Guide** in the app.

## Building

keypiano targets MSVC 2022 (x64) with dependencies managed by
[vcpkg](https://vcpkg.io) in manifest mode.

```powershell
# 1. Point VCPKG_ROOT at your vcpkg checkout
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

# 2. Configure + build (Debug, with unit tests)
cmake --preset windows-debug
cmake --build --preset windows-debug

# 3. Run the tests
ctest --preset test-debug
```

For a release build with the Qt GUI (and the VST3 host), use the
`windows-release` preset — it produces a self-contained `Release/` folder. See
[docs/build.md](docs/build.md) for full setup, deployment, and VST3 details.

## Architecture

Three threads, communicating through lock-free queues — never blocking the
audio path:

- **Hook thread** — a Win32 `WH_KEYBOARD_LL` low-level keyboard hook; looks up
  the key in the keymap and pushes a MIDI event onto the event queue.
- **Audio thread** — the RtAudio WASAPI callback; drains the event queue, drives
  FluidSynth, renders audio, and posts feedback for the UI.
- **UI thread** — the Qt main thread; polls the feedback queue every 16 ms to
  highlight keys, and posts user input (mouse/menu) onto the event queue.

The audio thread obeys a strict rule: **no allocation, no locks, no Qt calls,
no file I/O.**

## License

keypiano is free software, licensed under the **GNU General Public License
v3.0**. See [LICENSE](LICENSE) for the full text.
