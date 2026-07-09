#include "GLCanvas.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTabletEvent>
#include <algorithm>
#include <cmath>

namespace {

const char* kVertexShader = R"(
attribute vec2 aPos;
attribute vec2 aUV;
varying vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

const char* kFragmentShader = R"(
varying vec2 vUV;
uniform sampler2D uTex;
void main() {
    gl_FragColor = texture2D(uTex, vUV);
}
)";

}  // namespace

GLCanvas::GLCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    applyToolSettings();
}

GLCanvas::~GLCanvas() {
    makeCurrent();
    m_texture.reset();
    m_program.reset();
    if (m_vbo.isCreated()) m_vbo.destroy();
    doneCurrent();
}

void GLCanvas::setBitmap(core::Bitmap* bitmap) {
    m_bitmap = bitmap;
    m_textureNeedsRecreate = true;
    update();
}

void GLCanvas::setTool(Tool tool) {
    m_tool = tool;
    applyToolSettings();
}

void GLCanvas::applySettingsFor(Tool tool) {
    auto& settings = m_brush.settings();
    if (tool == Tool::Pen) {
        settings.radius = 6.0f;
        settings.color = {0, 0, 0, 255};
        settings.pressureAffectsRadius = true;
    } else {
        // 消しゴム: 紙(白)に戻す。ペンより太めで筆圧の影響は受けない
        settings.radius = 24.0f;
        settings.color = {255, 255, 255, 255};
        settings.pressureAffectsRadius = false;
    }
}

void GLCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aUV", 1);
    m_program->link();

    m_vbo.create();
    m_textureNeedsRecreate = true;
}

void GLCanvas::resizeGL(int w, int h) {
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void GLCanvas::recreateTexture() {
    m_texture.reset();
    if (!m_bitmap || m_bitmap->isEmpty()) return;

    m_texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_texture->setSize(m_bitmap->width(), m_bitmap->height());
    m_texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    m_texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Nearest);
    m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, m_bitmap->data());
    m_textureNeedsRecreate = false;
}

void GLCanvas::uploadDirty(const core::DirtyRect& rect) {
    if (!m_bitmap || rect.isEmpty()) return;

    makeCurrent();
    if (m_textureNeedsRecreate || !m_texture) {
        recreateTexture();
        doneCurrent();
        update();
        return;
    }

    // 部分領域を連続バッファへ詰め替えてからアップロードする
    const int w = rect.width();
    const int h = rect.height();
    m_uploadScratch.resize(static_cast<size_t>(w) * h * 4);
    const int stride = m_bitmap->width() * 4;
    const uint8_t* src = m_bitmap->data() + static_cast<size_t>(rect.y0) * stride + static_cast<size_t>(rect.x0) * 4;
    for (int row = 0; row < h; ++row) {
        std::copy(src + static_cast<size_t>(row) * stride, src + static_cast<size_t>(row) * stride + static_cast<size_t>(w) * 4,
                  m_uploadScratch.begin() + static_cast<size_t>(row) * w * 4);
    }
    m_texture->setData(rect.x0, rect.y0, 0, w, h, 1, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, m_uploadScratch.data());
    doneCurrent();
    update();
}

QRectF GLCanvas::imageRectInWidget() const {
    if (!m_bitmap || m_bitmap->isEmpty()) return {};

    const qreal ww = width();
    const qreal wh = height();
    const qreal iw = m_bitmap->width();
    const qreal ih = m_bitmap->height();
    const qreal scale = qMin(ww / iw, wh / ih);
    const qreal dw = iw * scale;
    const qreal dh = ih * scale;
    return {(ww - dw) * 0.5, (wh - dh) * 0.5, dw, dh};
}

QPointF GLCanvas::widgetToImage(QPointF widgetPos) const {
    const QRectF rect = imageRectInWidget();
    if (rect.isEmpty()) return {};
    const qreal scale = m_bitmap->width() / rect.width();
    return {(widgetPos.x() - rect.x()) * scale, (widgetPos.y() - rect.y()) * scale};
}

void GLCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_textureNeedsRecreate) recreateTexture();
    if (!m_texture || !m_program) return;

    // 画像の表示矩形をNDCへ変換した頂点(pos.xy + uv)を毎フレーム組み立てる
    const QRectF rect = imageRectInWidget();
    const qreal ww = width();
    const qreal wh = height();
    const auto toNdcX = [ww](qreal x) { return static_cast<float>(x / ww * 2.0 - 1.0); };
    const auto toNdcY = [wh](qreal y) { return static_cast<float>(1.0 - y / wh * 2.0); };

    const float x0 = toNdcX(rect.left());
    const float x1 = toNdcX(rect.right());
    const float y0 = toNdcY(rect.top());
    const float y1 = toNdcY(rect.bottom());

    // テクスチャの行0=画像上端なので、上端頂点にv=0を割り当てる
    const float vertices[] = {
        x0, y0, 0.0f, 0.0f,  //
        x1, y0, 1.0f, 0.0f,  //
        x0, y1, 0.0f, 1.0f,  //
        x1, y1, 1.0f, 1.0f,  //
    };

    m_program->bind();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));

    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    glActiveTexture(GL_TEXTURE0);
    m_texture->bind();
    m_program->setUniformValue("uTex", 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_texture->release();
    m_vbo.release();
    m_program->release();
}

void GLCanvas::pointerBegin(QPointF widgetPos, float pressure) {
    if (!m_bitmap) return;
    const QPointF img = widgetToImage(widgetPos);
    m_strokeActive = true;
    const auto dirty = m_brush.beginStroke(*m_bitmap, static_cast<float>(img.x()), static_cast<float>(img.y()), pressure);
    uploadDirty(dirty);
}

void GLCanvas::pointerMove(QPointF widgetPos, float pressure) {
    if (!m_bitmap || !m_strokeActive) return;
    const QPointF img = widgetToImage(widgetPos);
    const auto dirty = m_brush.continueStroke(*m_bitmap, static_cast<float>(img.x()), static_cast<float>(img.y()), pressure);
    uploadDirty(dirty);
}

void GLCanvas::pointerEnd() {
    m_strokeActive = false;
    m_brush.endStroke();
}

void GLCanvas::tabletEvent(QTabletEvent* event) {
    // ペンの後端(消しゴム側)で描いた場合は、UI上の選択ツールを変えずに消しゴムとして扱う
    const bool eraserPointer = event->pointerType() == QPointingDevice::PointerType::Eraser;
    applySettingsFor(eraserPointer ? Tool::Eraser : m_tool);

    switch (event->type()) {
        case QEvent::TabletPress:
            pointerBegin(event->position(), static_cast<float>(event->pressure()));
            break;
        case QEvent::TabletMove:
            pointerMove(event->position(), static_cast<float>(event->pressure()));
            break;
        case QEvent::TabletRelease:
            pointerEnd();
            break;
        default:
            break;
    }

    applyToolSettings();  // 選択中ツールの設定へ戻す
    event->accept();      // acceptすることで同内容のマウスイベント合成を抑止する
}

void GLCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->button() == Qt::LeftButton) pointerBegin(event->position(), 1.0f);
}

void GLCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->buttons() & Qt::LeftButton) pointerMove(event->position(), 1.0f);
}

void GLCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->button() == Qt::LeftButton) pointerEnd();
}

void GLCanvas::debugSimulateStroke() {
    // キャンバス左上寄りから右下へ、筆圧を変化させながら描く
    const qreal w = width();
    const qreal h = height();
    pointerBegin({w * 0.2, h * 0.3}, 0.2f);
    const int steps = 40;
    for (int i = 1; i <= steps; ++i) {
        const qreal t = static_cast<qreal>(i) / steps;
        const qreal x = w * (0.2 + 0.6 * t);
        const qreal y = h * (0.3 + 0.4 * t) + 30.0 * std::sin(t * 6.28);
        const float pressure = 0.2f + 0.8f * static_cast<float>(t);
        pointerMove({x, y}, pressure);
    }
    pointerEnd();
}
