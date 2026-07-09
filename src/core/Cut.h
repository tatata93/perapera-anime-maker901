#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Layer.h"

namespace core {

// シーン内の1カット。レイヤーを順序付きで保持する。将来的にXsheet(タイムシート)情報もここに持たせる。
class Cut {
public:
    explicit Cut(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Layer& addLayer(std::string name);
    void removeLayer(size_t index);

    size_t layerCount() const { return m_layers.size(); }
    Layer& layer(size_t index) { return *m_layers.at(index); }
    const Layer& layer(size_t index) const { return *m_layers.at(index); }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Layer>> m_layers;
};

}  // namespace core
