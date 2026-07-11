#include "Effect.h"

#include <iterator>

namespace core {

namespace {

// baseにoverrideを上書きマージしたものを返す(overrideに無いパラメータ名はbaseの値を使う)
std::map<std::string, double> mergeOverride(const std::map<std::string, double>& base,
                                             const std::map<std::string, double>& override) {
    std::map<std::string, double> result = base;
    for (const auto& [key, value] : override) result[key] = value;
    return result;
}

}  // namespace

std::map<std::string, double> Effect::paramsAt(size_t frame) const {
    if (paramKeys.empty()) return params;

    // frame以上の最初のキーを探す(Cut::cameraFrameAtと同じ規則)
    const auto upper = paramKeys.lower_bound(frame);
    if (upper == paramKeys.begin()) return mergeOverride(params, upper->second);          // 最初のキーより前
    if (upper == paramKeys.end()) return mergeOverride(params, std::prev(upper)->second);  // 最後のキーより後
    if (upper->first == frame) return mergeOverride(params, upper->second);                // キー上

    // 前後のキーで、パラメータ名ごとに線形補間する
    const auto lower = std::prev(upper);
    const float t = static_cast<float>(frame - lower->first) / static_cast<float>(upper->first - lower->first);
    const std::map<std::string, double> a = mergeOverride(params, lower->second);
    const std::map<std::string, double> b = mergeOverride(params, upper->second);

    std::map<std::string, double> result = a;
    for (const auto& [key, bValue] : b) {
        const auto it = result.find(key);
        const double aValue = it != result.end() ? it->second : bValue;
        result[key] = aValue + (bValue - aValue) * static_cast<double>(t);
    }
    return result;
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
