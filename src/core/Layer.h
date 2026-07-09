#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Frame.h"

namespace core {

// レイヤーの種別。実際のアニメ制作でのレイヤーの役割を表す
// Normal: 通常(主線や彩色)
// ColorTrace: 色トレス線(影や色の塗分け境界を示す指定線)。最終画には含めない
// Correction: 作監修正(作画監督が上から重ねる修正指示)。最終画には含めない
enum class LayerRole { Normal, ColorTrace, Correction };

// カット内の1レイヤー。フレーム(セル)を順序付きで保持する。
class Layer {
public:
    explicit Layer(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    // 可視フラグ(既定true)。falseの場合、キャンバス合成時にこのレイヤーは積まれない
    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    // レイヤー種別(既定Normal)
    LayerRole role() const { return m_role; }
    void setRole(LayerRole role) { m_role = role; }

    Frame& addFrame();
    Frame& insertFrame(size_t index);  // index位置に挿入(0..frameCount())
    void removeFrame(size_t index);

    size_t frameCount() const { return m_frames.size(); }
    Frame& frame(size_t index) { return *m_frames.at(index); }
    const Frame& frame(size_t index) const { return *m_frames.at(index); }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Frame>> m_frames;
    bool m_visible = true;
    LayerRole m_role = LayerRole::Normal;
};

}  // namespace core
