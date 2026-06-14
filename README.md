# keypiano

A modern Windows virtual piano, rewritten from scratch in C++20. It turns your
computer keyboard into a playable instrument with low-latency SF2 (SoundFont)
synthesis, performance recording/playback, a Qt6 GUI, and VST3 instrument
hosting.

keypiano takes inspiration from [FreePiano](https://github.com/freepiano/freepiano)
(notably its `.map` keyboard-layout format, which keypiano parses for
compatibility) but shares no code with it. It is an independent, GPL v3 project.

## Status

Under active development. Roadmap:

| Phase | Goal | State |
|-------|------|-------|
| 1 | Core engine — keys make sound, latency < 20 ms, no GUI | ✅ complete (41/41 tests, ~5.8 ms latency) |
| 2 | Recording & playback (`.kps` format) | ✅ complete (72/72 tests) |
| 3 | Qt6 Widgets GUI — 88-key visual piano | ✅ complete |
| 4 | VST3 instrument hosting + packaging | in progress (host, editor, packaging done) |

## Features (target)

- **Low latency** — RtAudio over WASAPI, lock-free event queues, target < 20 ms.
- **SoundFont synthesis** — FluidSynth 2.3+, load any `.sf2`.
- **FreePiano keymap compatibility** — parses both FreePiano 2.0 and 2.1 `.map`
  syntax.
- **Recording** — capture a performance to a readable, Git-friendly `.kps` text
  file with microsecond precision, and play it back.
- **VST3 instruments** (Phase 4) — host `.vst3` instrument plug-ins and, when
  the plug-in provides one, its native editor GUI. No VST SDK download required
  (pulled via vcpkg).

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
