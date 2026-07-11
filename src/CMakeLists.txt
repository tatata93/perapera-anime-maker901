add_subdirectory(core)

add_executable(perapera-anime-maker901
    main.cpp
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/FramePanel.cpp
    ui/FramePanel.h
    ui/LayerPanel.cpp
    ui/LayerPanel.h
    ui/CelPanel.cpp
    ui/CelPanel.h
    ui/TapPanel.cpp
    ui/TapPanel.h
    ui/CameraPanel.cpp
    ui/CameraPanel.h
    ui/ShootingWindow.cpp
    ui/ShootingWindow.h
    ui/StoryboardWindow.cpp
    ui/StoryboardWindow.h
    ui/SettingBoardWindow.cpp
    ui/SettingBoardWindow.h
    ui/EditWindow.cpp
    ui/EditWindow.h
    ui/ReferencePanel.cpp
    ui/ReferencePanel.h
    ui/PalettePanel.cpp
    ui/PalettePanel.h
    ui/XsheetPanel.cpp
    ui/XsheetPanel.h
    ui/ExportDialog.cpp
    ui/ExportDialog.h
    ui/CelSizeDialog.cpp
    ui/CelSizeDialog.h
    render/GLCanvas.cpp
    render/GLCanvas.h
    previz/GltfLoader.cpp
    previz/GltfLoader.h
    previz/PrevizSheetPanel.cpp
    previz/PrevizSheetPanel.h
    previz/PrevizViewport.cpp
    previz/PrevizViewport.h
    previz/PrevizWindow.cpp
    previz/PrevizWindow.h
)

# tinygltf(ヘッダオンリー、MIT)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
target_include_directories(perapera-anime-maker901 PRIVATE ${TINYGLTF_INCLUDE_DIRS})

target_link_libraries(perapera-anime-maker901 PRIVATE
    perapera_core
    Qt6::Widgets
    Qt6::OpenGLWidgets
)

target_include_directories(perapera-anime-maker901 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET perapera-anime-maker901 POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-compiler-runtime $<TARGET_FILE:perapera-anime-maker901>
    )
endif()
