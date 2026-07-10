#pragma once

#include <string>
#include <vector>

namespace previz {

// glTFから読み込んだ描画用メッシュ(ノード変換ベイク済み・静的)
struct MeshPrimitive {
    std::vector<float> vertices;    // x,y,z,nx,ny,nz のインターリーブ
    std::vector<uint32_t> indices;  // 三角形インデックス
    float color[4] = {0.8f, 0.8f, 0.85f, 1.0f};  // baseColorFactor(テクスチャは未対応)
};

struct MeshData {
    std::vector<MeshPrimitive> primitives;
};

// .gltf / .glb を読み込む。ノード階層の変換は頂点にベイクする。
// テクスチャ・スキニング・アニメーションはMVPでは未対応(将来対応)
bool loadGltfMesh(const std::string& path, MeshData& out, std::string* errorOut = nullptr);

// 組み込みプリミティブ: 1m角の箱(床の上、原点)。レイアウトのブロッキング用
MeshData makeBoxMeshData();

}  // namespace previz
