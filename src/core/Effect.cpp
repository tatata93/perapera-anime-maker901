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

bool Effect::activeAt(size_t frame) const {
    if (frame < static_cast<size_t>(startFrame)) return false;               // in点より前
    if (endFrame >= 0 && frame > static_cast<size_t>(endFrame)) return false;  // out点より後
    return true;
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
        case EffectType::ColorCorrect:
            return {{"brightness", 0.0}, {"contrast", 1.0}, {"saturation", 1.0}, {"hue", 0.0}};
        case EffectType::Diffusion:
            return {{"radius", 12.0}, {"strength", 0.5}};
        case EffectType::RadialBlur:
            return {{"centerX", 0.5}, {"centerY", 0.5}, {"amount", 0.02}, {"taps", 8.0}};
        case EffectType::Vignette:
            return {{"amount", 0.4}, {"softness", 0.5}};
        case EffectType::Grain:
            return {{"amount", 0.15}, {"size", 1.0}};
        case EffectType::ChromAb:
            return {{"amount", 2.0}};
        case EffectType::Film:
            // respR/G/B0..4: 各層(R/G/B)の分光応答カーブ(5点、横軸は入力光の強さ0,0.25,0.5,0.75,1固定)。
            // 既定は恒等(=入力そのまま記録する)なのでフィルムの発色には影響しない
            return {{"exposure", 0.0},   {"contrast", 0.35}, {"fade", 0.04},      {"warmth", 0.1},
                    {"crosstalk", 0.08}, {"grain", 0.25},    {"grainSize", 1.6},
                    {"respR0", 0.0},     {"respR1", 0.25},   {"respR2", 0.5},     {"respR3", 0.75}, {"respR4", 1.0},
                    {"respG0", 0.0},     {"respG1", 0.25},   {"respG2", 0.5},     {"respG3", 0.75}, {"respG4", 1.0},
                    {"respB0", 0.0},     {"respB1", 0.25},   {"respB2", 0.5},     {"respB3", 0.75}, {"respB4", 1.0}};
        case EffectType::AnaFlare:
            // アナモルフィック風: 明部を横に伸ばした青い筋+中心対称のゴースト(既定は青系)
            return {{"threshold", 210.0}, {"intensity", 0.8}, {"length", 220.0},     {"ghosts", 3.0},
                    {"ghostStrength", 0.5}, {"tintR", 0.35},  {"tintG", 0.6},        {"tintB", 1.0}};
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
        case EffectType::ColorCorrect:
            return "色調補正";
        case EffectType::Diffusion:
            return "ディフュージョン";
        case EffectType::RadialBlur:
            return "放射ブラー";
        case EffectType::Vignette:
            return "ビネット";
        case EffectType::Grain:
            return "グレイン";
        case EffectType::ChromAb:
            return "色収差";
        case EffectType::Film:
            return "フィルム";
        case EffectType::AnaFlare:
            return "アナモルフィックフレア";
    }
    return "";
}

}  // namespace core
