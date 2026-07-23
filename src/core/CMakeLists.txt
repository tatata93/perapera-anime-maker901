add_library(perapera_core STATIC
    Bitmap.cpp
    Bitmap.h
    BrushEngine.cpp
    BrushEngine.h
    FillTool.cpp
    FillTool.h
    LassoFillTool.cpp
    LassoFillTool.h
    Frame.h
    Layer.cpp
    Layer.h
    PaintLayer.h
    Cel.cpp
    Cel.h
    Previz.h
    Cut.cpp
    Cut.h
    Scene.cpp
    Scene.h
    Project.cpp
    Project.h
    Command.h
    ExposureSheetCommand.cpp
    ExposureSheetCommand.h
    Compositor.cpp
    Compositor.h
    Effect.cpp
    Effect.h
    EffectProcessor.cpp
    EffectProcessor.h
    Multiplane.cpp
    Multiplane.h
    Parallel.h
    BoxBlur.h
    CommandStack.cpp
    CommandStack.h
    ProjectIO.cpp
    ProjectIO.h
    StrokeCommand.cpp
    StrokeCommand.h
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

target_include_directories(perapera_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(perapera_core PRIVATE nlohmann_json::nlohmann_json ZLIB::ZLIB)
