# Vendored VST3 hosting sources

`module.cpp` and `module_win32.cpp` are copied verbatim from the Steinberg VST3
SDK (v3.8.0_build_66), the same version provided by the vcpkg `vst3sdk` port.

## Why vendored

The vcpkg `vst3sdk` port installs the hosting **headers** and prebuilt static
libs, but does **not** compile the module loader (`VST3::Hosting::Module::create`
and its Win32 implementation). Without these, a host cannot load a `.vst3`
bundle. We compile just these two files into `keypiano_core` (see
`src/core/CMakeLists.txt`, gated on `VST3_FOUND`).

They are built with `/std:c++17` because under C++20 `std::filesystem::u8string()`
returns `std::u8string`, which the SDK's `char`-based path handling rejects.

## License

These files are part of the VST3 SDK, dual-licensed under **GPLv3** or the
Steinberg proprietary license. keypiano is GPLv3, so we use the GPLv3 branch.
The original Steinberg license header is retained at the top of each file.
