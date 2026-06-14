add_library(keypiano_ui STATIC
    MainWindow.cpp
    MainWindow.ui
    bridge/AudioBridge.cpp
    widgets/PianoWidget.cpp
    widgets/KeyboardOverlayWidget.cpp
    widgets/Vst3EditorWindow.cpp
    dialogs/SoundFontDialog.cpp
    dialogs/KeyMapEditorDialog.cpp
    dialogs/SettingsDialog.cpp
)
# NOTE: resources/keypiano.qrc is compiled into the keypiano *executable*
# (see the root CMakeLists.txt), not into this static lib. If it lived here the
# resource-registration object could be dropped by the linker when the exe links
# this .lib (nothing references it), leaving all ":/..." resources missing.

target_include_directories(keypiano_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(keypiano_ui PUBLIC
    keypiano_core
    Qt6::Widgets
)
