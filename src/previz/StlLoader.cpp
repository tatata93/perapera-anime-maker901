#include "StlLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace previz {

namespace {

struct Triangle {
    float normal[3] = {0, 0, 0};
    float v[3][3] = {};
};

// STLは常にリトルエンディアン。本プロジェクトの対象環境(x86/x64)もリトルエンディアンなので
// そのままmemcpyでよい
float readLeFloat(const unsigned char* p) {
    float v;
    std::memcpy(&v, p, sizeof(float));
    return v;
}

uint32_t readLeU32(const unsigned char* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(uint32_t));
    return v;
}

// 三角形の面法線を頂点から計算する((v1-v0)×(v2-v0)を正規化)
void computeFaceNormal(const float v0[3], const float v1[3], const float v2[3], float outNormal[3]) {
    const float ax = v1[0] - v0[0], ay = v1[1] - v0[1], az = v1[2] - v0[2];
    const float bx = v2[0] - v0[0], by = v2[1] - v0[1], bz = v2[2] - v0[2];
    float nx = ay * bz - az * by;
    float ny = az * bx - ax * bz;
    float nz = ax * by - ay * bx;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-9f) {
        nx /= len;
        ny /= len;
        nz /= len;
    } else {
        // 退化三角形(面積ほぼ0)。上向きを仮の法線にしておく
        nx = 0.0f;
        ny = 1.0f;
        nz = 0.0f;
    }
    outNormal[0] = nx;
    outNormal[1] = ny;
    outNormal[2] = nz;
}

// バイナリSTL: 80byteヘッダ+uint32三角形数+三角形ごと(法線3+頂点3×3=12 float、uint16属性)
bool parseBinaryStl(const std::vector<unsigned char>& data, std::vector<Triangle>& triangles,
                    std::string* errorOut) {
    if (data.size() < 84) {
        if (errorOut) *errorOut = "STLファイルが小さすぎます(バイナリヘッダ不足)";
        return false;
    }
    const uint32_t count = readLeU32(data.data() + 80);
    const size_t expected = 84 + static_cast<size_t>(count) * 50;
    if (expected != data.size()) {
        if (errorOut) *errorOut = "STLバイナリのサイズが三角形数と一致しません";
        return false;
    }
    triangles.reserve(count);
    const unsigned char* p = data.data() + 84;
    for (uint32_t i = 0; i < count; ++i) {
        Triangle tri;
        tri.normal[0] = readLeFloat(p + 0);
        tri.normal[1] = readLeFloat(p + 4);
        tri.normal[2] = readLeFloat(p + 8);
        for (int v = 0; v < 3; ++v) {
            tri.v[v][0] = readLeFloat(p + 12 + v * 12 + 0);
            tri.v[v][1] = readLeFloat(p + 12 + v * 12 + 4);
            tri.v[v][2] = readLeFloat(p + 12 + v * 12 + 8);
        }
        triangles.push_back(tri);
        p += 50;  // 12 float(48byte) + uint16属性(2byte)
    }
    return true;
}

// ASCII STL: "facet normal nx ny nz / outer loop / vertex x y z ×3 / endloop / endfacet"の繰り返し
bool parseAsciiStl(const std::string& text, std::vector<Triangle>& triangles, std::string* errorOut) {
    std::istringstream iss(text);
    std::string token;
    Triangle current;
    int vertexIndex = 0;
    bool inFacet = false;
    while (iss >> token) {
        if (token == "facet") {
            iss >> token;  // "normal"
            iss >> current.normal[0] >> current.normal[1] >> current.normal[2];
            inFacet = true;
            vertexIndex = 0;
        } else if (token == "vertex") {
            if (vertexIndex >= 3) continue;  // 想定外(4頂点以上)は無視して読み飛ばす
            iss >> current.v[vertexIndex][0] >> current.v[vertexIndex][1] >> current.v[vertexIndex][2];
            ++vertexIndex;
        } else if (token == "endfacet") {
            if (inFacet && vertexIndex == 3) triangles.push_back(current);
            inFacet = false;
        }
        // solid/outer/loop/endloop/endsolidはスキップ
    }
    if (triangles.empty()) {
        if (errorOut) *errorOut = "STL(ASCII)から三角形を読み取れませんでした";
        return false;
    }
    return true;
}

}  // namespace

bool loadStlMesh(const std::string& path, MeshData& out, std::string* errorOut) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (errorOut) *errorOut = "STLファイルを開けません: " + path;
        return false;
    }
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (raw.empty()) {
        if (errorOut) *errorOut = "STLファイルが空です";
        return false;
    }

    // 判定: 先頭が"solid"でもバイナリとしてサイズが整合するならバイナリと判定する
    // (ASCIIヘッダを偽装したバイナリSTL対策)
    const bool looksAscii = raw.size() >= 5 && std::memcmp(raw.data(), "solid", 5) == 0;
    bool isBinarySizeMatch = false;
    if (raw.size() >= 84) {
        const uint32_t count = readLeU32(raw.data() + 80);
        isBinarySizeMatch = (84 + static_cast<size_t>(count) * 50 == raw.size());
    }

    std::vector<Triangle> triangles;
    std::string parseError;
    bool ok = false;
    if (!looksAscii || isBinarySizeMatch) {
        ok = parseBinaryStl(raw, triangles, &parseError);
    } else {
        const std::string text(raw.begin(), raw.end());
        ok = parseAsciiStl(text, triangles, &parseError);
    }
    if (!ok) {
        if (errorOut) *errorOut = parseError;
        return false;
    }
    if (triangles.empty()) {
        if (errorOut) *errorOut = "STLに三角形がありません";
        return false;
    }

    // 頂点をそのままvertices(x,y,z,nx,ny,nz)に積む。STLは面ごとに独立した3頂点を持つため
    // 頂点共有せず、インデックスは連番(0,1,2,3,...)でよい
    MeshPrimitive prim;
    prim.vertices.reserve(triangles.size() * 3 * 6);
    prim.indices.reserve(triangles.size() * 3);

    float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();

    uint32_t nextIndex = 0;
    for (const Triangle& tri : triangles) {
        float normal[3] = {tri.normal[0], tri.normal[1], tri.normal[2]};
        const float normalLenSq = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
        if (normalLenSq < 1e-12f) {
            // ファイルの法線が0ベクトルなら面法線を頂点から計算する
            computeFaceNormal(tri.v[0], tri.v[1], tri.v[2], normal);
        }
        for (int v = 0; v < 3; ++v) {
            const float x = tri.v[v][0], y = tri.v[v][1], z = tri.v[v][2];
            prim.vertices.insert(prim.vertices.end(), {x, y, z, normal[0], normal[1], normal[2]});
            prim.indices.push_back(nextIndex++);
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    }

    prim.color[0] = 0.55f;
    prim.color[1] = 0.65f;
    prim.color[2] = 0.85f;
    prim.color[3] = 1.0f;

    // 自動フィット: STLはCAD由来で単位/位置がバラバラなため、読込後に
    // 「最大寸法=1、床(min_y=0)に接地、X/Z中心を原点」へ均一スケール+平行移動して正規化する。
    // これで箱・円柱・球と同じ感覚(1m角相当)で即なぞれるサイズになる
    const float sizeX = maxX - minX, sizeY = maxY - minY, sizeZ = maxZ - minZ;
    const float maxDim = std::max({sizeX, sizeY, sizeZ, 1e-6f});
    const float scale = 1.0f / maxDim;
    const float centerX = (minX + maxX) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;

    for (size_t i = 0; i < prim.vertices.size(); i += 6) {
        prim.vertices[i + 0] = (prim.vertices[i + 0] - centerX) * scale;
        prim.vertices[i + 1] = (prim.vertices[i + 1] - minY) * scale;
        prim.vertices[i + 2] = (prim.vertices[i + 2] - centerZ) * scale;
    }

    out.primitives.clear();
    out.primitives.push_back(std::move(prim));
    return true;
}

}  // namespace previz
