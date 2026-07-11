#pragma once

#include <map>
#include <string>

namespace core {

// 撮影エフェクトの種類
enum class EffectType { Blur, Glow, Para, Shake };

// 撮影エフェクト1つ。カット単位のスタックとして順に適用される
struct Effect {
    EffectType type = EffectType::Blur;
    bool enabled = true;
    int targetCel = -1;  // -1=画面全体、0以上=そのセルのみに適用
    std::map<std::string, double> params;  // 種類ごとの既定はeffectDefaultParams(type)で取得
};

// エフェクト種類ごとの既定パラメータ
std::map<std::string, double> effectDefaultParams(EffectType type);

// UI表示用の日本語名
const char* effectTypeName(EffectType type);

}  // namespace core
