add_subdirectory(core)

set(PERAPERA_APP_SOURCES
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
    ui/FilmCurveWidget.cpp
    ui/FilmCurveWidget.h
    ui/StoryboardWindow.cpp
    ui/StoryboardWindow.h
    ui/SettingBoardWindow.cpp
    ui/SettingBoardWindow.h
    ui/EditWindow.cpp
    ui/EditWindow.h
    ui/ProjectManagerWindow.cpp
    ui/ProjectManagerWindow.h
    ui/NewProjectDialog.cpp
    ui/NewProjectDialog.h
    ui/NewCutDialog.cpp
    ui/NewCutDialog.h
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
    ui/CanvasSizeDialog.cpp
    ui/CanvasSizeDialog.h
    ui/PaintLayerUtils.cpp
    ui/PaintLayerUtils.h
    ui/FloatingCanvasWindow.cpp
    ui/FloatingCanvasWindow.h
    ui/RetroTheme.cpp
    ui/RetroTheme.h
    render/GLCanvas.cpp
    render/GLCanvas.h
    previz/GltfLoader.cpp
    previz/GltfLoader.h
    previz/StlLoader.cpp
    previz/StlLoader.h
    previz/PrevizSheetPanel.cpp
    previz/PrevizSheetPanel.h
    previz/PrevizViewport.cpp
    previz/PrevizViewport.h
    previz/PrevizWindow.cpp
    previz/PrevizWindow.h
)

# tinygltf(ヘッダオンリー、MIT)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")

function(add_perapera_app target_name)
    add_executable(${target_name} ${PERAPERA_APP_SOURCES})
    target_include_directories(${target_name} PRIVATE
        ${TINYGLTF_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target_name} PRIVATE
        perapera_core
        Qt6::Widgets
        Qt6::OpenGLWidgets
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-compiler-runtime $<TARGET_FILE:${target_name}>
        )
    endif()
endfunction()

add_perapera_app(perapera-anime-maker901)

add_perapera_app(perapera-anime-maker901-retro)
target_compile_definitions(perapera-anime-maker901-retro PRIVATE PERAPERA_RETRO_UI=1)
