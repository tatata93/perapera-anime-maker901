#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Cut.h"

namespace core {

// プロジェクト内の1シーン。カットを順序付きで保持する。
class Scene {
public:
    explicit Scene(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Cut& addCut(std::string name);
    void removeCut(size_t index);

    size_t cutCount() const { return m_cuts.size(); }
    Cut& cut(size_t index) { return *m_cuts.at(index); }
    const Cut& cut(size_t index) const { return *m_cuts.at(index); }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Cut>> m_cuts;
};

}  // namespace core
