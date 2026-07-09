add_library(perapera_core STATIC
    Bitmap.cpp
    Bitmap.h
    Frame.h
    Layer.cpp
    Layer.h
    Cut.cpp
    Cut.h
    Scene.cpp
    Scene.h
    Project.cpp
    Project.h
    Command.h
    CommandStack.cpp
    CommandStack.h
)

target_include_directories(perapera_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
