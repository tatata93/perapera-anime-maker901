cmake_minimum_required(VERSION 3.21)
project(perapera-anime-maker901 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 日本語Windows環境のMSVCはソースをCP932として読むため、UTF-8を明示する
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_subdirectory(src)

option(PERAPERA_BUILD_TESTS "Build unit tests" ON)
if(PERAPERA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
