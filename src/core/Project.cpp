#include "Project.h"

#include <algorithm>
#include <utility>

namespace core {

namespace {
constexpr int kMinCanvasSize = 16;
constexpr int kMaxCanvasSize = 8192;

void resizeBitmapCanvas(Bitmap& bitmap, int newWidth, int newHeight) {
    if (bitmap.isEmpty() ||
        (bitmap.width() == newWidth && bitmap.height() == newHeight)) {
        return;
    }
    Bitmap resized(newWidth, newHeight);
    resized.fill({0, 0, 0, 0});
    const int offsetX = (newWidth - bitmap.width()) / 2;
    const int offsetY = (newHeight - bitmap.height()) / 2;
    const int srcX0 = std::max(0, -offsetX);
    const int srcY0 = std::max(0, -offsetY);
    const int srcX1 = std::min(bitmap.width(), newWidth - offsetX);
    const int srcY1 = std::min(bitmap.height(), newHeight - offsetY);
    for (int y = srcY0; y < srcY1; ++y) {
        for (int x = srcX0; x < srcX1; ++x) {
            resized.setPixel(x + offsetX, y + offsetY, bitmap.pixel(x, y));
        }
    }
    bitmap = std::move(resized);
}
}  // namespace

Scene& Project::addScene(std::string name) {
    m_scenes.push_back(std::make_unique<Scene>(std::move(name)));
    return *m_scenes.back();
}

void Project::removeScene(size_t index) {
    m_scenes.erase(m_scenes.begin() + static_cast<ptrdiff_t>(index));
}

SettingBoard& Project::duplicateSettingBoard(size_t index, std::string name) {
    SettingBoard copy = m_settingBoards.at(index);
    copy.name = std::move(name);
    auto it =
        m_settingBoards.insert(m_settingBoards.begin() + static_cast<ptrdiff_t>(index + 1), std::move(copy));
    return *it;
}

void Project::setCanvasSize(int width, int height) {
    m_canvasWidth = std::clamp(width, kMinCanvasSize, kMaxCanvasSize);
    m_canvasHeight = std::clamp(height, kMinCanvasSize, kMaxCanvasSize);
}

void Project::resizeCanvas(int width, int height, bool resizeLinkedCels) {
    const int newWidth = std::clamp(width, kMinCanvasSize, kMaxCanvasSize);
    const int newHeight = std::clamp(height, kMinCanvasSize, kMaxCanvasSize);
    if (resizeLinkedCels && (newWidth != m_canvasWidth || newHeight != m_canvasHeight)) {
        for (auto& scene : m_scenes) {
            for (size_t cutIndex = 0; cutIndex < scene->cutCount(); ++cutIndex) {
                Cut& cut = scene->cut(cutIndex);
                for (size_t celIndex = 0; celIndex < cut.celCount(); ++celIndex) {
                    Cel& cel = cut.cel(celIndex);
                    if (cel.paperWidth() == 0 && cel.paperHeight() == 0) {
                        cel.resizeCanvasPaper(newWidth, newHeight);
                    }
                }
                for (Effect& effect : cut.effects()) {
                    resizeBitmapCanvas(effect.mask, newWidth, newHeight);
                }
                for (MultiplaneCelPlane& plane : cut.multiplane().planes) {
                    resizeBitmapCanvas(plane.distanceMap, newWidth, newHeight);
                }
                for (MultiplaneBacklight& backlight : cut.multiplane().backlights) {
                    resizeBitmapCanvas(backlight.mask, newWidth, newHeight);
                }
            }
        }
    }
    m_canvasWidth = newWidth;
    m_canvasHeight = newHeight;
}

}  // namespace core
