#pragma once

#include <string>

#include "previz/GltfLoader.h"

namespace previz {

// .stl (ASCII/バイナリを自動判別)を読み込む。
// STLはCAD由来で単位・位置がバラバラなため、読込後にバウンディングボックスで正規化する
// (最大寸法=1、床[min_y=0]に接地、X/Z中心を原点へ)。頂点は面ごとに独立(共有しない)。
bool loadStlMesh(const std::string& path, MeshData& out, std::string* errorOut = nullptr);

}  // namespace previz
