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
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0) {
        ok = loader.LoadBinaryFromFile(&model, &error, &warning, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &error, &warning, path);
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

}  // namespace previz
