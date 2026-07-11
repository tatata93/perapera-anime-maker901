#include "Effect.h"

#include <iterator>

namespace core {

double Effect::valueAt(const std::string& key, size_t frame, double fallback) const {
    const auto curveIt = paramCurves.find(key);
    if (curveIt == paramCurves.end() || curveIt->second.empty()) {
        // キーフレームが無いパラメータは基本値(params)を静的に使う
        const auto paramIt = params.find(key);
        return paramIt != params.end() ? paramIt->second : fallback;
    }

    const auto& curve = curveIt->second;
    // frame以上の最初のキーを探す(Cut::cameraFrameAtと同じクランプ規則)
    const auto upper = curve.lower_bound(frame);
    if (upper == curve.begin()) return upper->second;                 // 最初のキーより前
    if (upper == curve.end()) return std::prev(upper)->second;         // 最後のキーより後
    if (upper->first == frame) return upper->second;                   // キー上

    // 前後のキーで線形補間する
    const auto lower = std::prev(upper);
    const double t = static_cast<double>(frame - lower->first) / static_cast<double>(upper->first - lower->first);
    return lower->second + (upper->second - lower->second) * t;
}

std::map<std::string, double> Effect::paramsAt(size_t frame) const {
    std::map<std::string, double> result = params;
    // キーフレームを持つパラメータはvalueAtで解決した値で上書きする
    for (const auto& [key, curve] : paramCurves) {
        if (!curve.empty()) result[key] = valueAt(key, frame);
    }
    return result;
}

bool Effect::hasCurve(const std::string& key) const {
    const auto it = paramCurves.find(key);
    return it != paramCurves.end() && !it->second.empty();
}

bool Effect::hasKeyAt(const std::string& key, size_t frame) const {
    const auto it = paramCurves.find(key);
    return it != paramCurves.end() && it->second.count(frame) > 0;
}

void Effect::setKey(const std::string& key, size_t frame, double value) {
    paramCurves[key][frame] = value;
}

void Effect::removeKey(const std::string& key, size_t frame) {
    const auto it = paramCurves.find(key);
    if (it == paramCurves.end()) return;
    it->second.erase(frame);
    if (it->second.empty()) paramCurves.erase(it);  // 最後のキーを消したら曲線ごと削除
}

std::map<std::string, double> effectDefaultParams(EffectType type) {
    switch (type) {
        case EffectType::Blur:
            return {{"radius", 4.0}};
        case EffectType::Glow:
            return {{"threshold", 200.0}, {"radius", 8.0}, {"strength", 0.6}};
        case EffectType::Para:
            return {{"top", 0.25}, {"bottom", 0.0}, {"r", 0.0}, {"g", 0.0}, {"b", 0.0}};
        case EffectType::Shake:
            return {{"amplitudeX", 8.0}, {"amplitudeY", 8.0}, {"seed", 1.0}};
    }
    return {};
}

const char* effectTypeName(EffectType type) {
    switch (type) {
        case EffectType::Blur:
            return "ブラー";
        case EffectType::Glow:
            return "グロー";
        case EffectType::Para:
            return "パラ";
        case EffectType::Shake:
            return "シェイク";
    }
    return "";
}

}  // namespace core
