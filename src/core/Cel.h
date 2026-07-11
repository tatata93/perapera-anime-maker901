#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Layer.h"

namespace core {

// 2Dベクトル(セルのタップ/ペグ移動量、px)
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// カット内の1セル(Aセル/Bセル等、タイムシートの列に対応する作画単位)。
// セルの中にレイヤー(主線/色トレス線/彩色等)を順序付き(下→上)で保持する。
// セルのi枚目の絵 = 各レイヤーのフレームiの合成。
class Cel {
public:
    explicit Cel(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // 可視フラグ(既定true)。falseの場合、キャンバス合成時にこのセルは積まれない
    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    Layer& addLayer(std::string name);
    void removeLayer(size_t index);
    // レイヤーをfrom位置からto位置へ移動する(範囲外の場合は何もしない)
    void moveLayer(size_t from, size_t to);

    size_t layerCount() const { return m_layers.size(); }
    Layer& layer(size_t index) { return *m_layers.at(index); }
    const Layer& layer(size_t index) const { return *m_layers.at(index); }

    // 動画(絵)の枚数。レイヤー0のフレーム数を代表値とする(レイヤー間はコマ数同期前提)
    size_t drawingCount() const { return m_layers.empty() ? 0 : m_layers.front()->frameCount(); }

    // --- 露出表(タイムシートの列) ---
    // コマt(カット尺内の位置)に表示する動画番号。-1はセルなし(空欄)

    int exposure(size_t frame) const { return frame < m_exposure.size() ? m_exposure[frame] : -1; }
    void setExposure(size_t frame, int drawing);
    // 尺の変更に合わせて露出表の長さを揃える(伸長分は-1で埋める)
    void resizeExposure(size_t frameCount) { m_exposure.resize(frameCount, -1); }
    const std::vector<int>& exposures() const { return m_exposure; }

    // 一括コマ打ち: 動画0,1,2...をstepコマずつ順番に割り当てる(frameCountまで)。
    // 動画が尽きたら残りは最後の動画を維持する(標準的な止め)
    void applyStepPattern(int step, size_t frameCount);

    // --- 位置キー(タップ/ペグ移動) ---
    // コマ→セルの移動量(px)。キー間は線形補間(等速)、キーの外側は端のキーの値を維持。
    // キーが無ければ(0,0) = タップ位置そのまま

    Vec2 positionAt(size_t frame) const;
    void setPositionKey(size_t frame, Vec2 position) { m_positionKeys[frame] = position; }
    void removePositionKey(size_t frame) { m_positionKeys.erase(frame); }
    const std::map<size_t, Vec2>& positionKeys() const { return m_positionKeys; }

    // --- セルの用紙サイズ(引きセル用、px) ---
    // 0はキャンバスサイズに従う(既定)。背景セルなどをキャンバスより大きい紙にすると、
    // 位置キーでタップ位置をずらすことでパン(引き)を再現できる

    int paperWidth() const { return m_paperWidth; }
    int paperHeight() const { return m_paperHeight; }
    // 用紙サイズの値だけを設定する(ビットマップ自体のリサイズは行わない)
    void setPaperSize(int w, int h) {
        m_paperWidth = w;
        m_paperHeight = h;
    }
    // セル内の全レイヤー・全フレームの非空ビットマップを新サイズへ中央基準で移し替える
    // (はみ出す部分は切り捨て、余白は透明)。paperWidth/HeightもnewW/newHへ更新する
    void resizePaper(int newW, int newH);

private:
    std::string m_name;
    std::vector<std::unique_ptr<Layer>> m_layers;
    std::vector<int> m_exposure;
    std::map<size_t, Vec2> m_positionKeys;
    bool m_visible = true;
    int m_paperWidth = 0;   // 0=キャンバスサイズに従う
    int m_paperHeight = 0;  // 0=キャンバスサイズに従う
};

}  // namespace core
