# Building keypiano

This document covers a full from-scratch build on Windows.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio 2022 | 17.x | "Desktop development with C++" workload (MSVC v143, x64) |
| CMake | ≥ 3.25 | Bundled with VS 2022, or install standalone |
| vcpkg | recent | Manifest mode; see below |
| Git | any | To clone keypiano and vcpkg |

All dependencies are pulled in automatically by vcpkg from
[`vcpkg.json`](../vcpkg.json) — you do **not** install them by hand. FluidSynth
and RtAudio are always installed; the heavier ones are gated behind vcpkg
*features* so a headless Phase 1/2 build does not have to compile Qt:

| Feature | Brings in | Selected by preset |
|---------|-----------|--------------------|
| (core)  | FluidSynth, RtAudio | always |
| `tests` | GoogleTest | `windows-debug` |
| `gui`   | Qt6 (`qtbase` + widgets) | `windows-gui-debug`, `windows-release`, `windows-relwithdebinfo` |
| `vst3`  | Steinberg VST3 SDK (`vst3sdk`) | `windows-gui-debug`, `windows-release`, `windows-relwithdebinfo` |

Each preset requests its features via `VCPKG_MANIFEST_FEATURES` in
[`CMakePresets.json`](../CMakePresets.json), so you normally never select them by
hand. The GUI presets request `gui;vst3` together — the VST3 instrument host is
built whenever the GUI is, with **no manual SDK download** (see below).

## 1. Set up vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\dev\vcpkg"      # persist this in your environment
```

> **Baseline.** The dependency baseline is pinned in
> [`vcpkg-configuration.json`](../vcpkg-configuration.json) under
> `default-registry.baseline` (a vcpkg commit SHA) for reproducible versions. To
> move to a newer set of ports, update that SHA to your vcpkg checkout's commit:
>
> ```powershell
> git -C $env:VCPKG_ROOT rev-parse HEAD
> ```

## 2. Configure

The build is driven by [`CMakePresets.json`](../CMakePresets.json), which wires
in the vcpkg toolchain automatically (it reads `$env:VCPKG_ROOT`).

| Preset | Build type | Tests | GUI + VST3 |
|--------|-----------|-------|-----|
| `windows-debug` | Debug | ON | OFF (headless core, Phase 1/2 focus) |
| `windows-gui-debug` | Debug | OFF | ON (GUI development) |
| `windows-release` | Release `/O2 /GL /LTCG` | OFF | ON |
| `windows-relwithdebinfo` | RelWithDebInfo | OFF | ON |

```powershell
cmake --preset windows-debug
```

The first configure builds all vcpkg dependencies from source and may take a
while (Qt6 in particular). Subsequent configures reuse the binary cache.

## 3. Build

```powershell
cmake --build --preset windows-debug
```

Artifacts land in `build/windows-debug/`.

## 4. Run the tests

```powershell
ctest --preset test-debug
```

Tests are GoogleTest-based and only built when `KEYPIANO_ENABLE_TESTS=ON`
(the `windows-debug` preset enables it). Each test source is registered in
[`tests/CMakeLists.txt`](../tests/CMakeLists.txt) as its module lands.

## 5. Release build with GUI

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

The Release output in `build/windows-release/Release/` is **self-contained**:
it runs on a machine with no Visual Studio, no vcpkg and no Qt install. Two
mechanisms put the runtime next to `keypiano.exe`:

- vcpkg's applocal step copies the top-level dependency DLLs (`Qt6Core.dll`,
  `libfluidsynth-3.dll`, `rtaudio.dll`, ICU, etc.).
- A CMake post-build step deploys the Qt **plugins** — `platforms/`, `styles/`,
  `imageformats/`. This is required: without `platforms/qwindows.dll` Qt aborts
  at startup with *"no Qt platform plugin could be initialized"*. It uses
  `windeployqt` when present, and otherwise (the vcpkg Qt port does **not** ship
  `windeployqt`) copies the plugins straight from the vcpkg tree, picking the
  Debug or Release variant per build config.

To verify self-containment, run the exe with a minimal `PATH` (only
`C:\Windows\System32`); it should start normally. To distribute, zip the entire
`Release/` folder. The `.pdb` files in the plugin sub-folders are debug symbols
and can be deleted from a release archive.

## VST3 instrument host (Phase 4)

The VST3 host is built automatically with the GUI presets — **no manual SDK
download or path is required**. The Steinberg VST3 SDK is pulled by vcpkg via
the `vst3sdk` port (the `vst3` feature). keypiano is GPL v3 and uses the VST3
SDK's GPL v3 licensing branch.

> **Why not VST2?** Steinberg ended VST2 licensing in 2018 and forbids
> redistributing the VST2 SDK, so a new project cannot legally obtain it. VST3
> is the supported path and needs no out-of-band SDK.

Notes for working with the host:

- On Windows a `.vst3` plug-in is a **bundle folder** (`Foo.vst3/Contents/...`).
  In the app use *File → Open VST3 Instrument…* and select the `.vst3` folder.
  The picker opens at the standard system VST3 location
  (`C:\Program Files\Common Files\VST3`) so installed plug-ins are right there;
  it then remembers wherever you last browsed (stored under
  `HKCU\Software\keypiano\keypiano\vst3_dir`, which you may pre-seed/override).
- *File → Show Plugin Editor* (Ctrl+E) opens the plug-in's own GUI when it
  provides one. Plug-ins without a custom editor (e.g. the mda set) report that
  no editor is available.
- A handy free test instrument is **mda-vst3**, built by the same vcpkg port if
  you add the `plugin-examples` feature to `vst3sdk`; it installs under
  `<vcpkg>/installed/x64-windows/share/vst3sdk/VST3/Release/mda-vst3.vst3`.

### Vendored hosting sources

The vcpkg `vst3sdk` port installs the hosting headers and static libs but does
**not** compile the module loader (`VST3::Hosting::Module::create`). keypiano
vendors those two SDK files under
[`third_party/vst3_hosting/`](../third_party/vst3_hosting/) (GPL v3 branch) and
compiles them into `keypiano_core`. No action needed — this is automatic.

## Troubleshooting

- **`VCPKG_ROOT` not found** — the presets rely on the `VCPKG_ROOT` environment
  variable. Set it before running `cmake --preset`.
- **Qt6 not found** — ensure the first configure completed; Qt6 is provided by
  vcpkg, not a system install. A failed Qt build usually means insufficient
  disk space or a missing Windows SDK component.
- **`/WX` warnings break the build** — keypiano treats warnings as errors
  (`/W4 /WX`). Fix the warning rather than relaxing the flag.
