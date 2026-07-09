#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>
#include <vector>

#include "core/BrushEngine.h"

class QOpenGLShaderProgram;
class QOpenGLTexture;

// 作画キャンバス。core::Bitmapをテクスチャとして表示し、
// ペン(QTabletEvent / Windows Inkバックエンド)・マウスによる描画入力を受け付ける。
class GLCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class Tool { Pen, Eraser };

    explicit GLCanvas(QWidget* parent = nullptr);
    ~GLCanvas() override;

    // 表示・編集対象のビットマップを設定する(所有権は持たない)
    void setBitmap(core::Bitmap* bitmap);
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    // 端から端まで筆圧を変えながら1ストローク描く(動作確認用フック)
    void debugSimulateStroke();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void pointerBegin(QPointF widgetPos, float pressure);
    void pointerMove(QPointF widgetPos, float pressure);
    void pointerEnd();

    QRectF imageRectInWidget() const;
    QPointF widgetToImage(QPointF widgetPos) const;
    void applyToolSettings() { applySettingsFor(m_tool); }
    void applySettingsFor(Tool tool);
    void recreateTexture();
    void uploadDirty(const core::DirtyRect& rect);

    core::Bitmap* m_bitmap = nullptr;
    core::BrushEngine m_brush;
    Tool m_tool = Tool::Pen;
    bool m_strokeActive = false;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture> m_texture;
    QOpenGLBuffer m_vbo;
    bool m_textureNeedsRecreate = true;
    std::vector<uint8_t> m_uploadScratch;  // 部分アップロード用の連続バッファ
};
