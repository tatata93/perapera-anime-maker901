#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "previz/GltfLoader.h"
#include "previz/StlLoader.h"

namespace {

// 頂点1個ぶん(x,y,z,nx,ny,nz)を取り出す
struct V {
    float x, y, z, nx, ny, nz;
};

V vertexAt(const previz::MeshPrimitive& prim, size_t index) {
    const size_t base = index * 6;
    return {prim.vertices[base + 0], prim.vertices[base + 1], prim.vertices[base + 2],
            prim.vertices[base + 3], prim.vertices[base + 4], prim.vertices[base + 5]};
}

bool isUnitLength(float x, float y, float z, float tolerance = 0.01f) {
    const float len = std::sqrt(x * x + y * y + z * z);
    return std::abs(len - 1.0f) < tolerance;
}

std::filesystem::path makeTempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

void writeLeFloat(std::ofstream& out, float v) { out.write(reinterpret_cast<const char*>(&v), sizeof(float)); }
void writeLeU16(std::ofstream& out, uint16_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(uint16_t)); }
void writeLeU32(std::ofstream& out, uint32_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(uint32_t)); }

// メモリ上でバイナリSTL(三角形2個、CAD由来の大きな座標系[0,100]の板)を一時ファイルに書き出す。
// 三角形1は法線0ベクトル(面法線計算のテスト)、三角形2は法線ありでそのまま使われることを確認する
std::filesystem::path writeSampleBinaryStl(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    char header[80] = {};
    out.write(header, sizeof(header));
    writeLeU32(out, 2);  // 三角形数

    // 三角形1: 法線0ベクトル
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 100);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 100);
    writeLeFloat(out, 100);
    writeLeFloat(out, 0);
    writeLeU16(out, 0);

    // 三角形2: 法線あり(そのまま使われるはず)
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 1);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 100);
    writeLeFloat(out, 100);
    writeLeFloat(out, 0);
    writeLeFloat(out, 0);
    writeLeFloat(out, 100);
    writeLeFloat(out, 0);
    writeLeU16(out, 0);

    out.close();
    return path;
}

// 同じ2三角形をASCII STLとして書き出す
std::filesystem::path writeSampleAsciiStl(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "solid sample\n";
    out << "facet normal 0 0 0\n";
    out << "  outer loop\n";
    out << "    vertex 0 0 0\n";
    out << "    vertex 100 0 0\n";
    out << "    vertex 100 100 0\n";
    out << "  endloop\n";
    out << "endfacet\n";
    out << "facet normal 0 0 1\n";
    out << "  outer loop\n";
    out << "    vertex 0 0 0\n";
    out << "    vertex 100 100 0\n";
    out << "    vertex 0 100 0\n";
    out << "  endloop\n";
    out << "endfacet\n";
    out << "endsolid sample\n";
    out.close();
    return path;
}

}  // namespace

TEST_CASE("makeCylinderMeshData generates valid geometry", "[previz]") {
    const previz::MeshData data = previz::makeCylinderMeshData(16);
    REQUIRE(data.primitives.size() == 1);
    const previz::MeshPrimitive& prim = data.primitives[0];

    REQUIRE(prim.vertices.size() % 6 == 0);
    const size_t vertexCount = prim.vertices.size() / 6;
    REQUIRE(vertexCount > 0);
    REQUIRE(prim.indices.size() % 3 == 0);
    REQUIRE_FALSE(prim.indices.empty());

    for (size_t i = 0; i < vertexCount; ++i) {
        const V v = vertexAt(prim, i);
        // 半径0.5・高さ1のbbox内(円柱: y∈[0,1]、x/z∈[-0.5,0.5])
        REQUIRE(std::abs(v.x) <= 0.5f + 1e-4f);
        REQUIRE(std::abs(v.z) <= 0.5f + 1e-4f);
        REQUIRE(v.y >= -1e-4f);
        REQUIRE(v.y <= 1.0f + 1e-4f);
        REQUIRE(isUnitLength(v.nx, v.ny, v.nz));
    }
    for (uint32_t idx : prim.indices) {
        REQUIRE(idx < vertexCount);
    }
}

TEST_CASE("makeSphereMeshData generates valid geometry", "[previz]") {
    const previz::MeshData data = previz::makeSphereMeshData(12, 16);
    REQUIRE(data.primitives.size() == 1);
    const previz::MeshPrimitive& prim = data.primitives[0];

    REQUIRE(prim.vertices.size() % 6 == 0);
    const size_t vertexCount = prim.vertices.size() / 6;
    REQUIRE(vertexCount > 0);
    REQUIRE(prim.indices.size() % 3 == 0);
    REQUIRE_FALSE(prim.indices.empty());

    for (size_t i = 0; i < vertexCount; ++i) {
        const V v = vertexAt(prim, i);
        // 半径0.5、中心(0,0.5,0)のbbox内(床の上に接する=y∈[0,1])
        REQUIRE(v.x >= -0.5f - 1e-4f);
        REQUIRE(v.x <= 0.5f + 1e-4f);
        REQUIRE(v.y >= -1e-4f);
        REQUIRE(v.y <= 1.0f + 1e-4f);
        REQUIRE(v.z >= -0.5f - 1e-4f);
        REQUIRE(v.z <= 0.5f + 1e-4f);
        REQUIRE(isUnitLength(v.nx, v.ny, v.nz));
    }
    for (uint32_t idx : prim.indices) {
        REQUIRE(idx < vertexCount);
    }
}

TEST_CASE("loadStlMesh reads binary STL and auto-fits it", "[previz][stl]") {
    const auto path = writeSampleBinaryStl(makeTempPath("previz_test_binary.stl"));
    previz::MeshData data;
    std::string error;
    const bool ok = previz::loadStlMesh(path.string(), data, &error);
    std::filesystem::remove(path);

    REQUIRE(ok);
    REQUIRE(error.empty());
    REQUIRE(data.primitives.size() == 1);
    const previz::MeshPrimitive& prim = data.primitives[0];
    REQUIRE(prim.vertices.size() / 6 == 6);  // 三角形2個×3頂点(共有なし)
    REQUIRE(prim.indices.size() == 6);

    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (size_t i = 0; i < prim.vertices.size() / 6; ++i) {
        const V v = vertexAt(prim, i);
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
        REQUIRE(isUnitLength(v.nx, v.ny, v.nz));  // 法線0ベクトルは面法線が計算されているはず
    }
    REQUIRE(std::abs(minY) < 0.01f);                  // 床(min_y=0)に接地
    REQUIRE(std::abs((maxX - minX) - 1.0f) < 0.01f);  // 最大寸法=1に正規化
}

TEST_CASE("loadStlMesh reads ASCII STL", "[previz][stl]") {
    const auto path = writeSampleAsciiStl(makeTempPath("previz_test_ascii.stl"));
    previz::MeshData data;
    std::string error;
    const bool ok = previz::loadStlMesh(path.string(), data, &error);
    std::filesystem::remove(path);

    REQUIRE(ok);
    REQUIRE(error.empty());
    REQUIRE(data.primitives.size() == 1);
    REQUIRE(data.primitives[0].vertices.size() / 6 == 6);
    REQUIRE(data.primitives[0].indices.size() == 6);
}

TEST_CASE("loadStlMesh reports errors for missing and empty files", "[previz][stl]") {
    previz::MeshData data;
    std::string error;

    SECTION("存在しないファイル") {
        const bool ok = previz::loadStlMesh("C:/__previz_nonexistent_dir__/nope.stl", data, &error);
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(error.empty());
    }

    SECTION("空ファイル") {
        const auto path = makeTempPath("previz_test_empty.stl");
        std::ofstream(path, std::ios::binary).close();
        const bool ok = previz::loadStlMesh(path.string(), data, &error);
        std::filesystem::remove(path);
        REQUIRE_FALSE(ok);
        REQUIRE_FALSE(error.empty());
    }
}
