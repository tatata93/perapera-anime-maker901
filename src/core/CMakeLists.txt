add_library(perapera_core STATIC
    Bitmap.cpp
    Bitmap.h
    BrushEngine.cpp
    BrushEngine.h
    FillTool.cpp
    FillTool.h
    Frame.h
    Layer.cpp
    Layer.h
    Cel.cpp
    Cel.h
    Cut.cpp
    Cut.h
    Scene.cpp
    Scene.h
    Project.cpp
    Project.h
    Command.h
    Compositor.cpp
    Compositor.h
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
