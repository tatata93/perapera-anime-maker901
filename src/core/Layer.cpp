#include "Layer.h"

#include <utility>

namespace core {

Frame& Layer::addFrame() {
    m_frames.push_back(std::make_unique<Frame>());
    return *m_frames.back();
}

Frame& Layer::insertFrame(size_t index) {
    auto it = m_frames.insert(m_frames.begin() + static_cast<ptrdiff_t>(index), std::make_unique<Frame>());
    return **it;
}

void Layer::removeFrame(size_t index) {
    m_frames.erase(m_frames.begin() + static_cast<ptrdiff_t>(index));
}

std::unique_ptr<Layer> Layer::clone(std::string name) const {
    auto copy = std::make_unique<Layer>(std::move(name));
    copy->m_visible = m_visible;
    copy->m_opacity = m_opacity;
    copy->m_role = m_role;
    for (const auto& frame : m_frames) {
        copy->addFrame().bitmap() = frame->bitmap();
    }
    return copy;
}

}  // namespace core
