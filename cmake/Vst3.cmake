# cmake/Vst3.cmake — wires the vcpkg `vst3sdk` port (Phase 4 VST3 host).
#
# That port installs headers under include/vst3sdk and static libs under
# lib/vst3sdk (+ debug/lib/vst3sdk) but exports NO CMake config, so we build an
# INTERFACE imported target `keypiano::vst3` by hand.
#
# Sets VST3_FOUND. Silent no-op when the port is absent (e.g. headless P1/P2
# builds that don't enable the `vst3` vcpkg feature), so the core library simply
# omits the VST3 backend then.

find_path(VST3_INCLUDE_DIR
    NAMES public.sdk/source/vst/hosting/module.h
    PATH_SUFFIXES vst3sdk
)

if(VST3_INCLUDE_DIR)
    # include/vst3sdk → installed/<triplet>
    get_filename_component(_vst3_prefix "${VST3_INCLUDE_DIR}/../.." ABSOLUTE)
    set(_vst3_rel "${_vst3_prefix}/lib/vst3sdk")
    set(_vst3_dbg "${_vst3_prefix}/debug/lib/vst3sdk")

    if(NOT TARGET keypiano::vst3)
        add_library(keypiano::vst3 INTERFACE IMPORTED GLOBAL)
        target_include_directories(keypiano::vst3 INTERFACE
            "${VST3_INCLUDE_DIR}"
            # The vendored hosting sources (third_party/vst3_hosting) use
            # relative includes like "module.h" / "utility/*.h".
            "${VST3_INCLUDE_DIR}/public.sdk/source/vst/hosting")
        # Static link order matters: hosting → sdk → common → base → interfaces.
        foreach(_lib sdk_hosting sdk sdk_common base pluginterfaces)
            target_link_libraries(keypiano::vst3 INTERFACE
                "$<IF:$<CONFIG:Debug>,${_vst3_dbg}/${_lib}.lib,${_vst3_rel}/${_lib}.lib>")
        endforeach()
    endif()

    set(VST3_FOUND TRUE)
    message(STATUS "Found VST3 SDK (vcpkg): ${VST3_INCLUDE_DIR}")
else()
    set(VST3_FOUND FALSE)
    message(STATUS "VST3 SDK not found — Phase 4 (VST3 host) disabled.")
endif()
