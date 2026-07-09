#pragma once

#include <vector>

#include "BrushEngine.h"
#include "Command.h"

namespace core {

// 1ストローク分の可逆操作。書き換え矩形のbefore/after画素を保持する。
// 注意: 対象Bitmapへの生ポインタを持つため、フレーム構造の変更(追加/削除/読み込み)時は
// このコマンドを積んだCommandStackをclear()して破棄すること。
class StrokeCommand : public Command {
public:
    StrokeCommand(Bitmap* bitmap, DirtyRect region, std::vector<uint8_t> before, std::vector<uint8_t> after)
        : m_bitmap(bitmap), m_region(region), m_before(std::move(before)), m_after(std::move(after)) {}

    void execute() override { writeRegion(m_after); }
    void undo() override { writeRegion(m_before); }

    const DirtyRect& region() const { return m_region; }

    // 矩形領域の画素を連続バッファへ切り出す
    static std::vector<uint8_t> copyRegion(const Bitmap& bitmap, const DirtyRect& region);

private:
    void writeRegion(const std::vector<uint8_t>& pixels);

    Bitmap* m_bitmap;
    DirtyRect m_region;
    std::vector<uint8_t> m_before;
    std::vector<uint8_t> m_after;
};

}  // namespace core
