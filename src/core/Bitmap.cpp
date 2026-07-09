#include "Bitmap.h"

#include <cassert>

namespace core {

Bitmap::Bitmap(int width, int height)
    : m_width(width), m_height(height), m_pixels(static_cast<size_t>(width) * height * 4, 0) {}

Bitmap::Pixel Bitmap::pixel(int x, int y) const {
    assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
    const size_t offset = (static_cast<size_t>(y) * m_width + x) * 4;
    return Pixel{m_pixels[offset], m_pixels[offset + 1], m_pixels[offset + 2], m_pixels[offset + 3]};
}

void Bitmap::setPixel(int x, int y, Pixel color) {
    assert(x >= 0 && x < m_width && y >= 0 && y < m_height);
    const size_t offset = (static_cast<size_t>(y) * m_width + x) * 4;
    m_pixels[offset] = color.r;
    m_pixels[offset + 1] = color.g;
    m_pixels[offset + 2] = color.b;
    m_pixels[offset + 3] = color.a;
}

}  // namespace core
