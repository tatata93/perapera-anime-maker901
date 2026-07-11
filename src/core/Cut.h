#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Cel.h"
#include "Effect.h"
#include "Previz.h"

namespace core {

// カメラフレーム(画面に写る範囲)。centerはキャンバスpx座標、scaleは倍率
// (1.0=作画フレーム全体、0.5=半分の範囲を写す=2倍ズームイン)
struct CameraFrameState {
    Vec2 center;
    double scale = 1.0;
};

// 制作進捗(編集/カッティング工程の進行管理用)
enum class CutStatus { NotStarted, Layout, KeyAnimation, Inbetween, Finishing, Shooting, Done };

// シーン内の1カット。セル(Aセル/Bセル等)を順序付き(下→上)で保持する。
// 将来的にXsheet(タイムシート)情報もここに持たせる。
class Cut {
public:
    explicit Cut(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // 絵コンテメモ: 内容(アクション)・セリフ。既定は空文字
    const std::string& action() const { return m_action; }
    void setAction(std::string action) { m_action = std::move(action); }
    const std::string& dialogue() const { return m_dialogue; }
    void setDialogue(std::string dialogue) { m_dialogue = std::move(dialogue); }

    // 制作進捗(編集/カッティング工程の進行管理用)。既定は未着手
    CutStatus status() const { return m_status; }
    void setStatus(CutStatus status) { m_status = status; }

    // カットの尺(総コマ数)。タイムシートの行数に相当する
    size_t frameCount() const { return m_frameCount; }
    void setFrameCount(size_t count);

    Cel& addCel(std::string name);
    void removeCel(size_t index);
    // セルをfrom位置からto位置へ移動する(範囲外の場合は何もしない)
    void moveCel(size_t from, size_t to);

    size_t celCount() const { return m_cels.size(); }
    Cel& cel(size_t index) { return *m_cels.at(index); }
    const Cel& cel(size_t index) const { return *m_cels.at(index); }

    // プリビズシーン(3Dモデル配置+カメラ)。カット単位で持つ
    PrevizScene& previz() { return m_previz; }
    const PrevizScene& previz() const { return m_previz; }

    // --- カメラフレーム(レイアウト工程で画面に写る範囲を指定する、PAN/T.U.の基盤) ---
    // コマ→カメラフレーム。キーが無ければnullopt、1個以上あれば線形補間する
    // (最初のキーより前・最後のキーより後はクランプ)

    std::optional<CameraFrameState> cameraFrameAt(size_t frame) const;
    // scaleは0.05以上にクランプして登録する
    void setCameraKey(size_t frame, CameraFrameState state);
    void removeCameraKey(size_t frame) { m_cameraKeys.erase(frame); }
    void clearCameraKeys() { m_cameraKeys.clear(); }
    const std::map<size_t, CameraFrameState>& cameraKeys() const { return m_cameraKeys; }

    // --- 撮影エフェクト(ブラー/グロー/パラ/シェイク等) ---
    // カット単位のスタック。先頭から順に適用される(EffectProcessor::applyEffect参照)

    std::vector<Effect>& effects() { return m_effects; }
    const std::vector<Effect>& effects() const { return m_effects; }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Cel>> m_cels;
    PrevizScene m_previz;
    size_t m_frameCount = 1;  // 尺(最低1コマ)
    std::string m_action;     // 絵コンテ: 内容(アクション)
    std::string m_dialogue;   // 絵コンテ: セリフ
    std::map<size_t, CameraFrameState> m_cameraKeys;
    CutStatus m_status = CutStatus::NotStarted;  // 制作進捗(編集/カッティング工程の進行管理用)
    std::vector<Effect> m_effects;               // 撮影エフェクトのスタック(カット単位)
};

}  // namespace core
