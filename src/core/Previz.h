#pragma once

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace core {

// 3Dベクトル(プリビズ用。座標系: 右手系、Y上、単位はメートル相当の任意単位)
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// 3Dトランスフォーム(回転はオイラー角・度、適用順はY(ヨー)→X(ピッチ)→Z(ロール))
struct PrevizTransform {
    Vec3 position;
    Vec3 rotationDeg;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

namespace previz_detail {
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)};
}

// std::map<size_t, T>のコマキーを線形補間する共通処理(セルの位置キーと同じ規則)
template <typename T, typename LerpFn>
T interpolateKeys(const std::map<size_t, T>& keys, size_t frame, const T& fallback, LerpFn lerpFn) {
    if (keys.empty()) return fallback;
    const auto upper = keys.lower_bound(frame);
    if (upper == keys.begin()) return upper->second;
    if (upper == keys.end()) return std::prev(upper)->second;
    if (upper->first == frame) return upper->second;
    const auto lower = std::prev(upper);
    const float t = static_cast<float>(frame - lower->first) / static_cast<float>(upper->first - lower->first);
    return lerpFn(lower->second, upper->second, t);
}
}  // namespace previz_detail

// プリビズに配置する3Dモデル(glTFファイル参照+トランスフォーム)
struct PrevizModel {
    std::string name;
    std::string filePath;  // glTF/glbファイルパス
    PrevizTransform transform;                       // キーが無いときの基本配置
    std::map<size_t, PrevizTransform> transformKeys;  // コマ→トランスフォーム(モーション)

    PrevizTransform transformAt(size_t frame) const {
        return previz_detail::interpolateKeys(transformKeys, frame, transform,
                                              [](const PrevizTransform& a, const PrevizTransform& b, float t) {
                                                  return PrevizTransform{
                                                      previz_detail::lerp(a.position, b.position, t),
                                                      previz_detail::lerp(a.rotationDeg, b.rotationDeg, t),
                                                      previz_detail::lerp(a.scale, b.scale, t)};
                                              });
    }
};

// 物理カメラの状態(位置・向き・焦点距離)
struct PrevizCameraState {
    Vec3 position{0.0f, 1.0f, 5.0f};
    Vec3 rotationDeg;              // ピッチ(x)/ヨー(y)/ロール(z)
    float focalLengthMm = 50.0f;   // 焦点距離(mm)。望遠=パース圧縮、広角=遠近強調
};

// プリビズカメラ。センサー幅と焦点距離から画角を決める物理カメラモデル
struct PrevizCamera {
    PrevizCameraState state;                       // キーが無いときの基本状態
    float sensorWidthMm = 36.0f;                   // フルサイズ横幅(35mm判)
    // レンズ歪曲。0=歪みなし(直線を直線に写す標準レンズ)、正=樽型/魚眼(直線が外へ膨らむ)、
    // 負=糸巻き型。描画後の放射状ワープで表現する(post-process、値は概ね-0.5〜+1.5が実用域)
    float lensDistortion = 0.0f;
    std::map<size_t, PrevizCameraState> keys;      // コマ→カメラ状態(カメラワーク)

    PrevizCameraState stateAt(size_t frame) const {
        return previz_detail::interpolateKeys(
            keys, frame, state, [](const PrevizCameraState& a, const PrevizCameraState& b, float t) {
                return PrevizCameraState{previz_detail::lerp(a.position, b.position, t),
                                         previz_detail::lerp(a.rotationDeg, b.rotationDeg, t),
                                         previz_detail::lerp(a.focalLengthMm, b.focalLengthMm, t)};
            });
    }

    // 水平画角(度) = 2 * atan(センサー幅 / (2 * 焦点距離))
    float horizontalFovDeg(size_t frame) const {
        const float focal = stateAt(frame).focalLengthMm;
        return 2.0f * std::atan(sensorWidthMm / (2.0f * focal)) * 180.0f / 3.14159265358979f;
    }
};

// カットに紐づくプリビズシーン(3Dモデル群+カメラ)
struct PrevizScene {
    std::vector<PrevizModel> models;
    PrevizCamera camera;

    bool isEmpty() const { return models.empty() && camera.keys.empty(); }
};

}  // namespace core
