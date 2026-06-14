find_package(FluidSynth CONFIG REQUIRED)
find_package(RtAudio CONFIG REQUIRED)

add_library(keypiano_core STATIC
    audio/AudioEngine.cpp
    audio/AudioCallback.cpp
    synth/FluidSynthEngine.cpp
    synth/SynthFactory.cpp
    keymap/KeyMap.cpp
    keymap/KeyMapParser.cpp
    config/AppConfig.cpp
    config/CfgParser.cpp
    platform/win32/Win32KeyboardHook.cpp
    keymap/KeyMapSerializer.cpp
    recorder/KpsFormat.cpp
    recorder/Recorder.cpp
)

target_include_directories(keypiano_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(keypiano_core PUBLIC
    FluidSynth::libfluidsynth
    RtAudio::rtaudio
)

target_compile_features(keypiano_core PUBLIC cxx_std_20)

# ── VST3 backend (Phase 4) — only when the vst3sdk port is present ─────────────
if(VST3_FOUND)
    # The vcpkg vst3sdk port ships hosting headers + libs but does NOT compile the
    # module loader (Module::create / module_win32). We vendor those two SDK files
    # (GPLv3 branch) so .vst3 bundles can be loaded.
    target_sources(keypiano_core PRIVATE
        synth/Vst3SynthEngine.cpp
        "${CMAKE_SOURCE_DIR}/third_party/vst3_hosting/module.cpp"
        "${CMAKE_SOURCE_DIR}/third_party/vst3_hosting/module_win32.cpp")
    target_link_libraries(keypiano_core PRIVATE keypiano::vst3)
    target_compile_definitions(keypiano_core PUBLIC KEYPIANO_ENABLE_VST3=1)
    # SDK hosting sources are third-party: silence warnings (/W0) and build them
    # as C++17 — under C++20 std::filesystem::u8string() yields std::u8string,
    # which the SDK's char-based path handling does not accept.
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/third_party/vst3_hosting/module.cpp"
        "${CMAKE_SOURCE_DIR}/third_party/vst3_hosting/module_win32.cpp"
        PROPERTIES COMPILE_OPTIONS "/W0;/std:c++17")
endif()

# Sources are added incrementally per phase:
#
# P1-B  audio/LockFreeQueue.h (header-only, no entry needed)
# P1-C  audio/MidiEvent.h (header-only)
# P1-D  synth/FluidSynthEngine.cpp
# P1-E  audio/AudioEngine.cpp  audio/AudioCallback.cpp
# P1-F  platform/win32/Win32KeyboardHook.cpp
# P1-G  keymap/KeyMap.cpp  keymap/KeyMapParser.cpp  keymap/KeyMapSerializer.cpp
# P1-H  config/AppConfig.cpp  config/CfgParser.cpp
# P2-A  recorder/KpsFormat.cpp
# P2-B  recorder/Recorder.cpp
# P4-A  synth/Vst3SynthEngine.cpp + vendored module loader (when VST3_FOUND)
# P4-B  synth/IPluginEditor.h (header-only; editor support added to Vst3SynthEngine)
