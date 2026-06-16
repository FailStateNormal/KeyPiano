cmake_minimum_required(VERSION 3.25)

project(keypiano
    VERSION 1.0.0
    DESCRIPTION "Windows virtual piano — GPL v3"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/utf-8 /MP /W4 /WX)
    # Each option needs its own generator expression: a ';' inside one genex is
    # parsed by CMake as a list separator, which mangles the flags (it produced
    # a bogus '$<0:/O2' "source file" and a stray '/GL>'). Keep them separate.
    add_compile_options("$<$<CONFIG:Release>:/O2>" "$<$<CONFIG:Release>:/GL>")
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()

# ── Options ───────────────────────────────────────────────────────────────────
option(KEYPIANO_ENABLE_TESTS "Build GoogleTest unit tests" OFF)
option(KEYPIANO_ENABLE_GUI   "Build Qt6 Widgets GUI (Phase 3+)" ON)

# ── VST3 host (Phase 4) ───────────────────────────────────────────────────────
# Auto-detected: present only when the `vst3` vcpkg feature installed the SDK.
# Defines keypiano::vst3 + VST3_FOUND; headless builds simply skip it.
include(Vst3)

# ── Core library — pure C++20, zero Qt dependency ─────────────────────────────
add_subdirectory(src/core)

# ── UI library — Qt6 Widgets, depends on keypiano_core ────────────────────────
if(KEYPIANO_ENABLE_GUI)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
    # qt_standard_project_setup() enables AUTOMOC/AUTOUIC but NOT AUTORCC, so
    # without this the .qrc was never compiled — every ":/icons/..." and
    # ":/keymaps/..." resource was missing at runtime (blank toolbar icons and,
    # critically, the default keymap failed to load → keyboard did nothing).
    set(CMAKE_AUTORCC ON)
    add_subdirectory(src/ui)
endif()

# ── Main executable ───────────────────────────────────────────────────────────
if(KEYPIANO_ENABLE_GUI)
    # Generate a .rc file with the absolute path to the ICO so rc.exe can find it
    # regardless of the build working directory.
    file(WRITE "${CMAKE_BINARY_DIR}/keypiano_app.rc"
        "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/resources/icons/app.ico\"\n")
    # The .qrc is compiled into the executable itself (AUTORCC, enabled above) so
    # the embedded resources (:/icons, :/keymaps) are guaranteed to register.
    add_executable(keypiano src/main.cpp
        "${CMAKE_BINARY_DIR}/keypiano_app.rc"
        "${CMAKE_SOURCE_DIR}/resources/keypiano.qrc")
else()
    add_executable(keypiano src/main.cpp)
endif()
target_link_libraries(keypiano PRIVATE keypiano_core)

if(KEYPIANO_ENABLE_GUI)
    target_link_libraries(keypiano PRIVATE keypiano_ui)
    target_compile_definitions(keypiano PRIVATE KEYPIANO_ENABLE_GUI=1)
    set_target_properties(keypiano PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ── Deploy Qt plugins next to the exe ─────────────────────────────────────────
# Without the platform plugin (platforms/qwindows[d].dll) the app aborts at
# startup with "no Qt platform plugin could be initialized". vcpkg copies the
# top-level Qt DLLs but NOT the plugins, and the vcpkg Qt port ships no
# windeployqt. So: use windeployqt when available, else copy the essential
# plugin folders straight from the (config-specific) vcpkg_installed tree.
if(KEYPIANO_ENABLE_GUI)
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET keypiano POST_BUILD
            COMMAND "${WINDEPLOYQT}" "$<TARGET_FILE:keypiano>"
            COMMENT "Deploying Qt runtime via windeployqt..."
        )
    else()
        # Qt6_DIR = <vcpkg_installed>/<triplet>/share/Qt6
        #   release plugins: <triplet>/Qt6/plugins
        #   debug   plugins: <triplet>/debug/Qt6/plugins
        set(_qt_plugins
            "$<IF:$<CONFIG:Debug>,${Qt6_DIR}/../../debug/Qt6/plugins,${Qt6_DIR}/../../Qt6/plugins>")
        foreach(_sub platforms styles imageformats)
            add_custom_command(TARGET keypiano POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${_qt_plugins}/${_sub}"
                    "$<TARGET_FILE_DIR:keypiano>/${_sub}"
                COMMENT "Deploying Qt plugins: ${_sub}"
            )
        endforeach()
    endif()

    # ── Strip Qt's plugin .pdb debug symbols from the deploy dir ──────────────
    # The deploy steps above drag Qt's own plugin .pdb into platforms/styles/
    # imageformats (~46 MB). They're useless to end users and to us (Qt's
    # symbols, not ours — our debug pdb stay in build/). Scoped to the plugin
    # subfolders by strip_qt_pdb.cmake so our own keypiano.pdb is never touched.
    add_custom_command(TARGET keypiano POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DDIR=$<TARGET_FILE_DIR:keypiano>"
            -P "${CMAKE_SOURCE_DIR}/cmake/strip_qt_pdb.cmake"
        COMMENT "Stripping useless Qt plugin .pdb from deploy dir"
    )

    # ── Bundle the default piano SoundFont next to the exe ────────────────────
    # GeneralUser GS is a real sampled piano (~30 MB) — too large to embed in the
    # .qrc, so it ships as a loose file in a soundfonts/ folder beside the exe.
    # loadDefaultSoundFont() loads it at startup and falls back to the tiny
    # synthetic piano embedded in the .qrc if it is absent. Guarded by EXISTS so
    # a checkout without the file still builds.
    set(_default_sf2 "${CMAKE_SOURCE_DIR}/resources/soundfonts/GeneralUser-GS.sf2")
    if(EXISTS "${_default_sf2}")
        add_custom_command(TARGET keypiano POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_default_sf2}"
                "$<TARGET_FILE_DIR:keypiano>/soundfonts/GeneralUser-GS.sf2"
            COMMENT "Bundling default SoundFont: GeneralUser-GS.sf2"
        )
    endif()
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────
if(KEYPIANO_ENABLE_TESTS)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()
