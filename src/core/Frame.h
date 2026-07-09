#pragma once

#include "Bitmap.h"

namespace core {

// カット内の1コマ(セル)。
class Frame {
public:
    Frame() = default;
    explicit Frame(Bitmap bitmap) : m_bitmap(std::move(bitmap)) {}

    Bitmap& bitmap() { return m_bitmap; }
    const Bitmap& bitmap() const { return m_bitmap; }

private:
    Bitmap m_bitmap;
};

}  // namespace core
