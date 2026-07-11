#include "Effect.h"

namespace core {

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
