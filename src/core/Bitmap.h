#pragma once

#include <cstdint>
#include <vector>

namespace core {

// RGBA8のピクセルバッファ。OpenGLテクスチャへのアップロードを想定した生データレイアウト。
class Bitmap {
public:
    Bitmap() = default;
    Bitmap(int width, int height);

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool isEmpty() const { return m_width <= 0 || m_height <= 0; }

    const uint8_t* data() const { return m_pixels.data(); }
    uint8_t* data() { return m_pixels.data(); }
    size_t byteSize() const { return m_pixels.size(); }

    struct Pixel {
        uint8_t r = 0, g = 0, b = 0, a = 0;
    };

    Pixel pixel(int x, int y) const;
    void setPixel(int x, int y, Pixel color);

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<uint8_t> m_pixels;
};

}  // namespace core
