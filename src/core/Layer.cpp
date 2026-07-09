#include "Layer.h"

namespace core {

Frame& Layer::addFrame() {
    m_frames.push_back(std::make_unique<Frame>());
    return *m_frames.back();
}

void Layer::removeFrame(size_t index) {
    m_frames.erase(m_frames.begin() + static_cast<ptrdiff_t>(index));
}

}  // namespace core
