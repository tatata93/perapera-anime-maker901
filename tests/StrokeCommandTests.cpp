#include <catch2/catch_test_macros.hpp>

#include "core/BrushEngine.h"
#include "core/CommandStack.h"
#include "core/StrokeCommand.h"

namespace {

core::Bitmap makeWhiteBitmap(int w, int h) {
    core::Bitmap bitmap(w, h);
    bitmap.fill({255, 255, 255, 255});
    return bitmap;
}

}  // namespace

TEST_CASE("StrokeCommand undoes and redoes a brush stroke", "[core][command]") {
    auto bitmap = makeWhiteBitmap(64, 64);
    core::Bitmap snapshot = bitmap;  // ストローク開始時点のコピー

    core::BrushEngine engine;
    engine.settings().radius = 5.0f;
    auto dirty = engine.beginStroke(bitmap, 20.0f, 32.0f, 1.0f);
    dirty.unite(engine.continueStroke(bitmap, 44.0f, 32.0f, 1.0f));
    engine.endStroke();
    REQUIRE(bitmap.pixel(32, 32).r < 100);  // 線が引かれている

    auto before = core::StrokeCommand::copyRegion(snapshot, dirty);
    auto after = core::StrokeCommand::copyRegion(bitmap, dirty);

    core::CommandStack stack;
    stack.push(std::make_unique<core::StrokeCommand>(&bitmap, dirty, std::move(before), std::move(after)));
    REQUIRE(bitmap.pixel(32, 32).r < 100);  // push(execute)後も線は残る(冪等)

    stack.undo();
    REQUIRE(bitmap.pixel(32, 32).r == 255);  // 線が消えて白に戻る
    REQUIRE(bitmap.pixel(20, 32).r == 255);

    stack.redo();
    REQUIRE(bitmap.pixel(32, 32).r < 100);  // 線が復元される
}

TEST_CASE("CommandStack clear drops history", "[core][command]") {
    auto bitmap = makeWhiteBitmap(16, 16);
    core::Bitmap snapshot = bitmap;

    core::BrushEngine engine;
    engine.settings().radius = 3.0f;
    const auto dirty = engine.beginStroke(bitmap, 8.0f, 8.0f, 1.0f);
    engine.endStroke();

    core::CommandStack stack;
    stack.push(std::make_unique<core::StrokeCommand>(&bitmap, dirty, core::StrokeCommand::copyRegion(snapshot, dirty),
                                                     core::StrokeCommand::copyRegion(bitmap, dirty)));
    REQUIRE(stack.canUndo());

    stack.clear();
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}
