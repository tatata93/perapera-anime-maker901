#pragma once

#include <map>
#include <string>

namespace core {

// 撮影エフェクトの種類
enum class EffectType { Blur, Glow, Para, Shake };

// 撮影エフェクト1つ。カット単位のスタックとして順に適用される。
// キーフレームはAfter Effects同様にプロパティ(パラメータ)単位で持つ:
// あるパラメータにキーが1つも無ければparams(基本値)を静的に使い、
// キーがあればそのパラメータだけコマ間を線形補間する(他パラメータには影響しない)。
struct Effect {
    EffectType type = EffectType::Blur;
    bool enabled = true;
    int targetCel = -1;  // -1=画面全体、0以上=そのセルのみに適用
    std::map<std::string, double> params;  // 種類ごとの既定はeffectDefaultParams(type)で取得

    // パラメータ名 → (コマ→値) のキーフレーム曲線。キーの無いパラメータはparamsの基本値を使う。
    // キーが1個ならその値で一定、2個以上ならコマ間を線形補間(範囲外はクランプ)
    std::map<std::string, std::map<size_t, double>> paramCurves;

    // 指定パラメータのコマframeでの値を返す。キーが無ければparams(無ければfallback)
    double valueAt(const std::string& key, size_t frame, double fallback = 0.0) const;
    // コマframeで有効なパラメータ一式を返す(各パラメータをvalueAtで解決)
    std::map<std::string, double> paramsAt(size_t frame) const;

    // 指定パラメータにキーフレームがあるか
    bool hasCurve(const std::string& key) const;
    // 指定パラメータのコマframeにキーがあるか
    bool hasKeyAt(const std::string& key, size_t frame) const;
    // 指定パラメータのコマframeにキーを打つ(既存なら上書き)
    void setKey(const std::string& key, size_t frame, double value);
    // 指定パラメータのコマframeのキーを消す(最後の1個を消したら曲線ごと消える)
    void removeKey(const std::string& key, size_t frame);
};

// エフェクト種類ごとの既定パラメータ
std::map<std::string, double> effectDefaultParams(EffectType type);

// UI表示用の日本語名
const char* effectTypeName(EffectType type);

}  // namespace core
