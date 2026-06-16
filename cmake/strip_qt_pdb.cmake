# Removes Qt's own plugin .pdb debug symbols from a deployed app directory.
#
# windeployqt (and our manual plugin copy in the top-level CMakeLists) drag the
# .pdb files that ship alongside Qt's plugins into platforms/, styles/ and
# imageformats/ — about 46 MB uncompressed. Those symbols are useless to end
# users *and* to us: they describe Qt's internals, not our code. Our own debug
# symbols (keypiano.pdb) live next to the build objects and are never deployed
# here, so we deliberately scope the deletion to the plugin subfolders to keep
# any of our own pdb untouched.
#
# Invoked as: cmake -DDIR=<exe deploy dir> -P strip_qt_pdb.cmake
foreach(_sub platforms styles imageformats)
    file(GLOB_RECURSE _pdbs "${DIR}/${_sub}/*.pdb")
    if(_pdbs)
        file(REMOVE ${_pdbs})
        message(STATUS "strip_qt_pdb: removed ${_sub} symbols: ${_pdbs}")
    endif()
endforeach()
