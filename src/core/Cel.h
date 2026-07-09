#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Layer.h"

namespace core {

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

private:
    std::string m_name;
    std::vector<std::unique_ptr<Layer>> m_layers;
    bool m_visible = true;
};

}  // namespace core
