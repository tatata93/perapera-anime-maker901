#include "StrokeCommand.h"

#include <cstring>

namespace core {

std::vector<uint8_t> StrokeCommand::copyRegion(const Bitmap& bitmap, const DirtyRect& region) {
    const int w = region.width();
    const int h = region.height();
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    const size_t stride = static_cast<size_t>(bitmap.width()) * 4;
    const uint8_t* src = bitmap.data() + static_cast<size_t>(region.y0) * stride + static_cast<size_t>(region.x0) * 4;
    for (int row = 0; row < h; ++row) {
        std::memcpy(out.data() + static_cast<size_t>(row) * w * 4, src + static_cast<size_t>(row) * stride,
                    static_cast<size_t>(w) * 4);
    }
    return out;
}

void StrokeCommand::writeRegion(const std::vector<uint8_t>& pixels) {
    const int w = m_region.width();
    const int h = m_region.height();
    const size_t stride = static_cast<size_t>(m_bitmap->width()) * 4;
    uint8_t* dst = m_bitmap->data() + static_cast<size_t>(m_region.y0) * stride + static_cast<size_t>(m_region.x0) * 4;
    for (int row = 0; row < h; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * stride, pixels.data() + static_cast<size_t>(row) * w * 4,
                    static_cast<size_t>(w) * 4);
    }
}

}  // namespace core
