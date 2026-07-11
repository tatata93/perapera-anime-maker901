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

    // コマ→パラメータ一式のキー(撮影シートのキー)。キーが無ければparams(基本値)を使う。
    // キーが1個以上あれば、コマ間は線形補間する(最初のキーより前・最後のキーより後はクランプ、
    // Cut::cameraFrameAtと同じ規則)。キーの中に無いパラメータ名はparamsの基本値で補う
    std::map<size_t, std::map<std::string, double>> paramKeys;

    // コマframeで有効なパラメータ一式を返す(補間・基本値補完込み)
    std::map<std::string, double> paramsAt(size_t frame) const;
};

// エフェクト種類ごとの既定パラメータ
std::map<std::string, double> effectDefaultParams(EffectType type);

// UI表示用の日本語名
const char* effectTypeName(EffectType type);

}  // namespace core
