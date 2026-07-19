#pragma once

#include <QImage>
#include <QSize>
#include <vector>

#include "core/PaintLayer.h"

namespace perapera::ui {

QSize paintLayerCanvasSize(const std::vector<core::PaintLayer>& layers, const core::Bitmap& legacy, int defaultWidth,
                           int defaultHeight);
void ensurePaintLayers(std::vector<core::PaintLayer>& layers, size_t& activeLayer, const core::Bitmap& legacy,
                       int defaultWidth, int defaultHeight, bool includePaintAndTrace);
void resizePaintLayersCentered(std::vector<core::PaintLayer>& layers, int width, int height);
core::Bitmap compositePaintLayers(const std::vector<core::PaintLayer>& layers, int width, int height);
QImage bitmapToImageCopy(const core::Bitmap& bitmap);

}  // namespace perapera::ui
