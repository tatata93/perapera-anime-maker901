find_package(Catch2 3 CONFIG REQUIRED)

add_executable(perapera_core_tests
    CoreModelTests.cpp
    CommandStackTests.cpp
    ExposureSheetCommandTests.cpp
    BrushEngineTests.cpp
    CompositorTests.cpp
    EffectTests.cpp
    FillToolTests.cpp
    LassoFillToolTests.cpp
    ProjectIOTests.cpp
    StrokeCommandTests.cpp
    MultiplaneTests.cpp
    PrevizMeshTests.cpp
    ../src/previz/GltfLoader.cpp
    ../src/previz/StlLoader.cpp
)

# GltfLoader.cppはtinygltf(ヘッダオンリー)に依存する。previz名前空間自体はQt非依存なので
# テスト実行ファイルへ直接コンパイルできる
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
target_include_directories(perapera_core_tests PRIVATE ${TINYGLTF_INCLUDE_DIRS})

target_link_libraries(perapera_core_tests PRIVATE
    perapera_core
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(perapera_core_tests)
