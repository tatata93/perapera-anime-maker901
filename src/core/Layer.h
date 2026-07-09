#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Frame.h"

namespace core {

// カット内の1レイヤー。フレーム(セル)を順序付きで保持する。
class Layer {
public:
    explicit Layer(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Frame& addFrame();
    Frame& insertFrame(size_t index);  // index位置に挿入(0..frameCount())
    void removeFrame(size_t index);

    size_t frameCount() const { return m_frames.size(); }
    Frame& frame(size_t index) { return *m_frames.at(index); }
    const Frame& frame(size_t index) const { return *m_frames.at(index); }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Frame>> m_frames;
};

}  // namespace core
