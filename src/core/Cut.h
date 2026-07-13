#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Cel.h"
#include "Effect.h"
#include "Multiplane.h"
#include "Previz.h"

namespace core {

// カメラフレーム(画面に写る範囲)。centerはキャンバスpx座標、scaleは倍率
// (1.0=作画フレーム全体、0.5=半分の範囲を写す=2倍ズームイン)
struct CameraFrameState {
    Vec2 center;
    double scale = 1.0;
};

// クラシック撮影(マルチプレーン撮影台)のセル1枚分の段割付
struct MultiplaneCelPlane {
    int celIndex = 0;          // 対象セル(インデックス)
    double distanceMm = 500.0;  // レンズから平面までの距離
    double widthMm = 400.0;     // アートワークの物理幅
};

// カット単位のクラシック撮影設定。enabled=falseなら従来のデジタル合成
struct MultiplaneSetup {
    bool enabled = false;
    MultiplaneCamera camera;
    std::vector<MultiplaneCelPlane> planes;    // 割付の無いセルは撮影されない
    int samplesPerPixel = 8;                   // 作業/プレビュー基準のサンプル数(プレビューはさらに上限4に絞られる)
    int exportSamplesPerPixel = 64;            // 書き出し時のサンプル数(高いほどレイトレースのノイズが減ってなめらか)
    std::vector<MultiplaneBacklight> backlights;  // 透過光(T光)。灯ごとに色/強度/マスク/点滅を持つ複数灯

    // フレーミング固定: trueなら「基準距離framingRefDistanceMmの平面上で写る幅framingWidthMm」から
    // センサー幅を導出する(sensorW = framingWidthMm * focal / framingRefDistanceMm)。
    // 焦点距離を変えても基準距離での構図が変わらない(望遠=パース圧縮・大ボケでも構図維持)。
    // falseなら従来どおりcamera.sensorWidthMmを直接使う
    bool framingLock = true;
    double framingWidthMm = 360.0;       // 既定は従来のフレーミング(focal50/sensor36/500mm)と一致
    double framingRefDistanceMm = 500.0;

    // コマ→値のキーフレーム曲線(キー間は線形補間、範囲外クランプ。空なら基本値を使う)。
    // 蛍光灯や液晶の点滅(押井守作品風)は隣接コマに0↔強度のキーを打つことで再現する(灯ごとの点滅は
    // MultiplaneBacklight::intensityKeysを使う)
    std::map<size_t, double> focalKeys;   // カメラ焦点距離mm(camera.focalLengthMmの代わり)
    std::map<size_t, double> focusKeys;   // カメラフォーカス距離mm(camera.focusDistanceMmの代わり)
    std::map<size_t, double> fstopKeys;   // 絞りF値(camera.apertureFStopの代わり)

    // キーフレーム曲線からコマframeの値を解決する(キーが無ければbaseをそのまま返す)。
    // 規則はEffect::valueAtと同じ(キー間線形補間、範囲外は端のキーでクランプ)
    static double valueAt(const std::map<size_t, double>& keys, size_t frame, double base);
};

// 制作進捗(編集/カッティング工程の進行管理用)
enum class CutStatus { NotStarted, Layout, KeyAnimation, Inbetween, Finishing, Shooting, Done };

// シーン内の1カット。セル(Aセル/Bセル等)を順序付き(下→上)で保持する。
// 将来的にXsheet(タイムシート)情報もここに持たせる。
class Cut {
public:
    explicit Cut(std::string name) : m_name(std::move(name)) {}

    // 永続ID(既定0=未割当)。並べ替えや改名でファイル参照(cuts/cut_<id>.ppam)が
    // 壊れないための恒久的な識別子。ProjectIO::save時に未割当なら採番される
    uint64_t id() const { return m_id; }
    void setId(uint64_t id) { m_id = id; }

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

    // --- クラシック撮影(マルチプレーン撮影台) ---
    // enabled=falseなら従来のデジタル合成(renderCutFrameはバイト同一を保証する)

    MultiplaneSetup& multiplane() { return m_multiplane; }
    const MultiplaneSetup& multiplane() const { return m_multiplane; }

private:
    uint64_t m_id = 0;  // 永続ID(既定0=未割当)
    std::string m_name;
    std::vector<std::unique_ptr<Cel>> m_cels;
    PrevizScene m_previz;
    size_t m_frameCount = 1;  // 尺(最低1コマ)
    std::string m_action;     // 絵コンテ: 内容(アクション)
    std::string m_dialogue;   // 絵コンテ: セリフ
    std::map<size_t, CameraFrameState> m_cameraKeys;
    CutStatus m_status = CutStatus::NotStarted;  // 制作進捗(編集/カッティング工程の進行管理用)
    std::vector<Effect> m_effects;               // 撮影エフェクトのスタック(カット単位)
    MultiplaneSetup m_multiplane;                 // クラシック撮影(マルチプレーン撮影台)設定
};

// setup.backlightsの先頭の灯を返す(空なら1灯追加してから返す)。既存の単灯前提UIを
// 「先頭の灯を編集する」形に繋ぐためのヘルパー(本格的な複数灯UIは別タスク)
MultiplaneBacklight& firstBacklight(Cut& cut);

}  // namespace core
