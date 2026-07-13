// テクスチャは扱わないためstb_imageを無効化する(依存削減)
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include "GltfLoader.h"

#include <tiny_gltf.h>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace previz {

namespace {

using Mat4 = std::array<float, 16>;  // 列優先(glTF準拠)

Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = sum;
        }
    }
    return r;
}

// glTFノードのTRS(回転はクォータニオン[x,y,z,w])から行列を作る
Mat4 nodeLocalMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        Mat4 m;
        for (int i = 0; i < 16; ++i) m[i] = static_cast<float>(node.matrix[i]);
        return m;
    }

    float tx = 0, ty = 0, tz = 0, sx = 1, sy = 1, sz = 1;
    float qx = 0, qy = 0, qz = 0, qw = 1;
    if (node.translation.size() == 3) {
        tx = static_cast<float>(node.translation[0]);
        ty = static_cast<float>(node.translation[1]);
        tz = static_cast<float>(node.translation[2]);
    }
    if (node.rotation.size() == 4) {
        qx = static_cast<float>(node.rotation[0]);
        qy = static_cast<float>(node.rotation[1]);
        qz = static_cast<float>(node.rotation[2]);
        qw = static_cast<float>(node.rotation[3]);
    }
    if (node.scale.size() == 3) {
        sx = static_cast<float>(node.scale[0]);
        sy = static_cast<float>(node.scale[1]);
        sz = static_cast<float>(node.scale[2]);
    }

    // 回転行列(クォータニオン→3x3)にスケールを掛け、平行移動を置く
    const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    const float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    const float wx = qw * qx, wy = qw * qy, wz = qw * qz;
    Mat4 m = identity();
    m[0] = (1 - 2 * (yy + zz)) * sx;
    m[1] = (2 * (xy + wz)) * sx;
    m[2] = (2 * (xz - wy)) * sx;
    m[4] = (2 * (xy - wz)) * sy;
    m[5] = (1 - 2 * (xx + zz)) * sy;
    m[6] = (2 * (yz + wx)) * sy;
    m[8] = (2 * (xz + wy)) * sz;
    m[9] = (2 * (yz - wx)) * sz;
    m[10] = (1 - 2 * (xx + yy)) * sz;
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
    return m;
}

// アクセサの要素ポインタを返す(byteStride対応)
const unsigned char* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t index,
                                  size_t elementSize) {
    const auto& view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[view.buffer];
    const size_t stride = view.byteStride > 0 ? view.byteStride : elementSize;
    return buffer.data.data() + view.byteOffset + accessor.byteOffset + index * stride;
}

// ダミー画像ローダ(テクスチャ未対応のため常に成功扱い)
bool dummyLoadImage(tinygltf::Image*, const int, std::string*, std::string*, int, int, const unsigned char*, int,
                    void*) {
    return true;
}

void appendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const Mat4& world,
                     MeshData& out) {
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES) return;
    const auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end()) return;

    const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
    const tinygltf::Accessor* normalAccessor = nullptr;
    if (const auto normalIt = primitive.attributes.find("NORMAL"); normalIt != primitive.attributes.end()) {
        normalAccessor = &model.accessors[normalIt->second];
    }

    MeshPrimitive prim;

    // マテリアル色(pbrのbaseColorFactor)
    if (primitive.material >= 0) {
        const auto& factor = model.materials[primitive.material].pbrMetallicRoughness.baseColorFactor;
        for (int i = 0; i < 4 && i < static_cast<int>(factor.size()); ++i) {
            prim.color[i] = static_cast<float>(factor[i]);
        }
    }

    // 頂点(ノード変換をベイク。法線は回転・スケール部のみ適用)
    prim.vertices.reserve(posAccessor.count * 6);
    for (size_t i = 0; i < posAccessor.count; ++i) {
        const float* p = reinterpret_cast<const float*>(accessorData(model, posAccessor, i, sizeof(float) * 3));
        const float wx = world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12];
        const float wy = world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13];
        const float wz = world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14];

        float nx = 0, ny = 1, nz = 0;
        if (normalAccessor) {
            const float* n = reinterpret_cast<const float*>(accessorData(model, *normalAccessor, i, sizeof(float) * 3));
            nx = world[0] * n[0] + world[4] * n[1] + world[8] * n[2];
            ny = world[1] * n[0] + world[5] * n[1] + world[9] * n[2];
            nz = world[2] * n[0] + world[6] * n[1] + world[10] * n[2];
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
        }
        prim.vertices.insert(prim.vertices.end(), {wx, wy, wz, nx, ny, nz});
    }

    // インデックス(無ければ連番)
    if (primitive.indices >= 0) {
        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        prim.indices.reserve(indexAccessor.count);
        for (size_t i = 0; i < indexAccessor.count; ++i) {
            uint32_t value = 0;
            switch (indexAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    value = *accessorData(model, indexAccessor, i, 1);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                    uint16_t v16;
                    std::memcpy(&v16, accessorData(model, indexAccessor, i, 2), 2);
                    value = v16;
                    break;
                }
                default: {
                    std::memcpy(&value, accessorData(model, indexAccessor, i, 4), 4);
                    break;
                }
            }
            prim.indices.push_back(value);
        }
    } else {
        for (uint32_t i = 0; i < static_cast<uint32_t>(posAccessor.count); ++i) prim.indices.push_back(i);
    }

    out.primitives.push_back(std::move(prim));
}

void traverseNode(const tinygltf::Model& model, int nodeIndex, const Mat4& parent, MeshData& out) {
    const tinygltf::Node& node = model.nodes[nodeIndex];
    const Mat4 world = multiply(parent, nodeLocalMatrix(node));
    if (node.mesh >= 0) {
        for (const auto& primitive : model.meshes[node.mesh].primitives) {
            appendPrimitive(model, primitive, world, out);
        }
    }
    for (const int child : node.children) traverseNode(model, child, world, out);
}

}  // namespace

bool loadGltfMesh(const std::string& path, MeshData& out, std::string* errorOut) {
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(dummyLoadImage, nullptr);

    tinygltf::Model model;
    std::string error;
    std::string warning;
    bool ok = false;

    // tinygltfのLoadASCIIFromFile/LoadBinaryFromFileはstd::stringのパスをそのまま
    // std::ifstreamへ渡すため、呼び出し元(PrevizWindow::addModel)がQString::toStdString()で
    // 渡してくるUTF-8パスに日本語などの非ASCII文字が含まれると、Windowsの実行時ANSIコードページ
    // (日本語環境では通常CP932)で誤って解釈されファイルを開けない(StlLoaderと同根の不具合)。
    // ここではファイル自体をこちらでUTF-8対応の方法で読み込み、tinygltfへはメモリ経由で渡すことで
    // ファイルオープン時点のエンコーディング不整合を回避する。
    // (ASCII .gltfが外部参照する.bin/テクスチャのURI解決はtinygltf内部のまま行われるため、
    // それらの置き場所に非ASCII文字がある場合はなお失敗しうる=既知の残課題)
    std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!file) {
        if (errorOut) *errorOut = "glTFファイルを開けません: " + path;
        return false;
    }
    const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    if (raw.empty()) {
        if (errorOut) *errorOut = "glTFファイルが空です";
        return false;
    }
    const size_t slash = path.find_last_of("/\\");
    const std::string baseDir = slash == std::string::npos ? std::string() : path.substr(0, slash);

    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0) {
        ok = loader.LoadBinaryFromMemory(&model, &error, &warning, raw.data(), static_cast<unsigned int>(raw.size()),
                                         baseDir);
    } else {
        ok = loader.LoadASCIIFromString(&model, &error, &warning, reinterpret_cast<const char*>(raw.data()),
                                        static_cast<unsigned int>(raw.size()), baseDir);
    }
    if (!ok) {
        if (errorOut) *errorOut = error.empty() ? "glTFの読み込みに失敗しました" : error;
        return false;
    }

    out.primitives.clear();
    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex >= static_cast<int>(model.scenes.size())) {
        if (errorOut) *errorOut = "glTFにシーンがありません";
        return false;
    }
    for (const int nodeIndex : model.scenes[sceneIndex].nodes) {
        traverseNode(model, nodeIndex, identity(), out);
    }

    if (out.primitives.empty()) {
        if (errorOut) *errorOut = "描画可能なメッシュが見つかりません";
        return false;
    }
    return true;
}

MeshData makeBoxMeshData() {
    // 1m角の箱(原点、床の上)。面ごとの法線付き
    MeshPrimitive prim;
    prim.vertices = {
        // 前面(+z)
        -0.5f, 0, 0.5f, 0, 0, 1,  0.5f, 0, 0.5f, 0, 0, 1,  0.5f, 1, 0.5f, 0, 0, 1,  -0.5f, 1, 0.5f, 0, 0, 1,
        // 背面(-z)
        -0.5f, 0, -0.5f, 0, 0, -1,  0.5f, 0, -0.5f, 0, 0, -1,  0.5f, 1, -0.5f, 0, 0, -1,  -0.5f, 1, -0.5f, 0, 0, -1,
        // 左(-x)
        -0.5f, 0, -0.5f, -1, 0, 0,  -0.5f, 0, 0.5f, -1, 0, 0,  -0.5f, 1, 0.5f, -1, 0, 0,  -0.5f, 1, -0.5f, -1, 0, 0,
        // 右(+x)
        0.5f, 0, -0.5f, 1, 0, 0,  0.5f, 0, 0.5f, 1, 0, 0,  0.5f, 1, 0.5f, 1, 0, 0,  0.5f, 1, -0.5f, 1, 0, 0,
        // 上(+y)
        -0.5f, 1, -0.5f, 0, 1, 0,  0.5f, 1, -0.5f, 0, 1, 0,  0.5f, 1, 0.5f, 0, 1, 0,  -0.5f, 1, 0.5f, 0, 1, 0,
        // 下(-y)
        -0.5f, 0, -0.5f, 0, -1, 0,  0.5f, 0, -0.5f, 0, -1, 0,  0.5f, 0, 0.5f, 0, -1, 0,  -0.5f, 0, 0.5f, 0, -1, 0,
    };
    prim.indices = {0,  1,  2,  0,  2,  3,  4,  6,  5,  4,  7,  6,  8,  9,  10, 8,  10, 11,
                    12, 14, 13, 12, 15, 14, 16, 17, 18, 16, 18, 19, 20, 22, 21, 20, 23, 22};
    prim.color[0] = 0.55f;
    prim.color[1] = 0.65f;
    prim.color[2] = 0.85f;
    prim.color[3] = 1.0f;

    MeshData data;
    data.primitives.push_back(std::move(prim));
    return data;
}

MeshData makeCylinderMeshData(int segments) {
    // 半径0.5・高さ1の円柱(床の上y∈[0,1]、原点中心)
    segments = std::max(3, segments);
    const float radius = 0.5f;
    const float twoPi = 6.28318530718f;

    MeshPrimitive prim;

    // 側面: セグメントごとに4頂点(下左/下右/上右/上左)。法線はセグメント中央の外向き放射方向で
    // 面ごとにフラットシェーディングする(箱と同じ流儀)
    for (int i = 0; i < segments; ++i) {
        const float a0 = twoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = twoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float x0 = radius * std::cos(a0), z0 = radius * std::sin(a0);
        const float x1 = radius * std::cos(a1), z1 = radius * std::sin(a1);
        const float amid = (a0 + a1) * 0.5f;
        const float nx = std::cos(amid), nz = std::sin(amid);

        const uint32_t base = static_cast<uint32_t>(prim.vertices.size() / 6);
        prim.vertices.insert(prim.vertices.end(), {x0, 0.0f, z0, nx, 0.0f, nz});
        prim.vertices.insert(prim.vertices.end(), {x1, 0.0f, z1, nx, 0.0f, nz});
        prim.vertices.insert(prim.vertices.end(), {x1, 1.0f, z1, nx, 0.0f, nz});
        prim.vertices.insert(prim.vertices.end(), {x0, 1.0f, z0, nx, 0.0f, nz});
        prim.indices.insert(prim.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // 上面(y=1、法線+Y): 中心+リングの扇状
    {
        const uint32_t centerIdx = static_cast<uint32_t>(prim.vertices.size() / 6);
        prim.vertices.insert(prim.vertices.end(), {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const uint32_t ringStart = static_cast<uint32_t>(prim.vertices.size() / 6);
        for (int i = 0; i <= segments; ++i) {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            prim.vertices.insert(prim.vertices.end(),
                                 {radius * std::cos(a), 1.0f, radius * std::sin(a), 0.0f, 1.0f, 0.0f});
        }
        for (int i = 0; i < segments; ++i) {
            prim.indices.insert(prim.indices.end(),
                                {centerIdx, ringStart + static_cast<uint32_t>(i), ringStart + static_cast<uint32_t>(i + 1)});
        }
    }

    // 下面(y=0、法線-Y): 中心+リングの扇状(巻き順を反転)
    {
        const uint32_t centerIdx = static_cast<uint32_t>(prim.vertices.size() / 6);
        prim.vertices.insert(prim.vertices.end(), {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f});
        const uint32_t ringStart = static_cast<uint32_t>(prim.vertices.size() / 6);
        for (int i = 0; i <= segments; ++i) {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            prim.vertices.insert(prim.vertices.end(),
                                 {radius * std::cos(a), 0.0f, radius * std::sin(a), 0.0f, -1.0f, 0.0f});
        }
        for (int i = 0; i < segments; ++i) {
            prim.indices.insert(prim.indices.end(),
                                {centerIdx, ringStart + static_cast<uint32_t>(i + 1), ringStart + static_cast<uint32_t>(i)});
        }
    }

    prim.color[0] = 0.55f;
    prim.color[1] = 0.65f;
    prim.color[2] = 0.85f;
    prim.color[3] = 1.0f;

    MeshData data;
    data.primitives.push_back(std::move(prim));
    return data;
}

MeshData makeSphereMeshData(int stacks, int slices) {
    // 半径0.5のUV球(床の上に接する、中心y=0.5)。緯度stacks×経度slicesのグリッド
    stacks = std::max(2, stacks);
    slices = std::max(3, slices);
    const float radius = 0.5f;
    const float centerY = 0.5f;
    const float pi = 3.14159265358979f;
    const float twoPi = 6.28318530718f;

    MeshPrimitive prim;
    // 頂点: (stacks+1)×(slices+1)。縫い目のためslices+1本目(u=1)を複製する
    for (int i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);  // 0(北極)〜1(南極)
        const float phi = v * pi;
        const float y = std::cos(phi);
        const float r = std::sin(phi);
        for (int j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * twoPi;
            const float x = r * std::cos(theta);
            const float z = r * std::sin(theta);
            // 法線=中心からの方向(単位球上の点そのもの)
            prim.vertices.insert(prim.vertices.end(), {x * radius, y * radius + centerY, z * radius, x, y, z});
        }
    }

    const int ringVerts = slices + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * ringVerts + j);
            const uint32_t b = static_cast<uint32_t>(i * ringVerts + j + 1);
            const uint32_t c = static_cast<uint32_t>((i + 1) * ringVerts + j + 1);
            const uint32_t d = static_cast<uint32_t>((i + 1) * ringVerts + j);
            prim.indices.insert(prim.indices.end(), {a, b, c, a, c, d});
        }
    }

    prim.color[0] = 0.55f;
    prim.color[1] = 0.65f;
    prim.color[2] = 0.85f;
    prim.color[3] = 1.0f;

    MeshData data;
    data.primitives.push_back(std::move(prim));
    return data;
}

}  // namespace previz
