#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Cel.h"
#include "Previz.h"

namespace core {

// シーン内の1カット。セル(Aセル/Bセル等)を順序付き(下→上)で保持する。
// 将来的にXsheet(タイムシート)情報もここに持たせる。
class Cut {
public:
    explicit Cut(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

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

private:
    std::string m_name;
    std::vector<std::unique_ptr<Cel>> m_cels;
    PrevizScene m_previz;
    size_t m_frameCount = 1;  // 尺(最低1コマ)
};

}  // namespace core
