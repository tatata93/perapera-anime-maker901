#include "GLCanvas.h"

#include <QImage>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTabletEvent>
#include <QVector3D>
#include <QVector4D>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/FillTool.h"
#include "core/StrokeCommand.h"

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

// uUnderlayMix > 0   : 下敷き(参照画像)。白との混合を乗算ブレンドで重ねることで
//                      下敷きの色はそのまま薄く透けて見える(ライトテーブル表現)。
// uOnionStrength > 0 : 線(暗部)をuTint色に置き換えた「白地+色線」を出力する。
//                      乗算ブレンドで重ねると白地は無視され色線だけが合成される。
// どちらも0            : テクスチャをそのまま表示(通常描画)
// uSolidWhite > 0    : 紙(白)。テクスチャを使わず白を出力する
// uUseSolidColor > 0 : テクスチャを使わずuSolidColor(alpha込み)をそのまま出力する。
//                      レイアウト用フレーム枠ガイドの線描画に使う
const char* kFragmentShader = R"(
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec3 uTint;
uniform float uOnionStrength;
uniform float uUnderlayMix;
uniform float uSolidWhite;
uniform float uUseSolidColor;
uniform vec4 uSolidColor;
uniform float uOpacity;
void main() {
    if (uUseSolidColor > 0.5) {
        gl_FragColor = uSolidColor;
        return;
    }
    if (uSolidWhite > 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    vec4 tex = texture2D(uTex, vUV);
    if (uUnderlayMix > 0.0) {
        gl_FragColor = vec4(mix(vec3(1.0), tex.rgb, uUnderlayMix), 1.0);
    } else if (uOnionStrength > 0.0) {
        // 透明セル対応: 塗られている(alpha)×暗さ でカバレッジを決める。
        // 旧形式の白不透明セル(alpha=1, 白=darkness 0)でも従来どおり白地は無視される
        float darkness = 1.0 - min(min(tex.r, tex.g), tex.b);
        float coverage = tex.a * darkness * uOnionStrength;
        gl_FragColor = vec4(mix(vec3(1.0), uTint, coverage), 1.0);
    } else {
        gl_FragColor = vec4(tex.rgb, tex.a * uOpacity);
    }
}
)";

constexpr float kOnionStrength = 0.55f;
const QVector3D kPrevTint(0.95f, 0.35f, 0.35f);       // 前フレーム: 赤系
const QVector3D kNextTint(0.30f, 0.75f, 0.35f);       // 次フレーム: 緑系
const QVector3D kLightTableTint(0.35f, 0.45f, 0.90f);  // ライトテーブル: 青系(オニオンと区別)

// レイアウト用フレーム枠ガイドの色(alpha 0.6程度の半透明)
const QVector4D kFrameGuideColor(0.25f, 0.85f, 0.25f, 0.6f);      // 作画フレーム(100%): 緑系
const QVector4D kTvSafeGuideColor(0.90f, 0.82f, 0.15f, 0.6f);     // TVセーフ(約90%): 黄系
const QVector4D kTitleSafeGuideColor(0.85f, 0.20f, 0.20f, 0.6f);  // タイトルセーフ(約80%): 赤系

// カメラフレーム(画面に写る範囲)枠オーバーレイの色: シアン(0,180,255)、不透明
const QVector4D kCameraFrameOverlayColor(0.0f, 180.0f / 255.0f, 1.0f, 1.0f);

}  // namespace

GLCanvas::GLCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    applyToolSettings();
}

GLCanvas::~GLCanvas() {
    makeCurrent();
    m_textures.clear();
    m_overlayTexture.reset();
    m_underlayTexture.reset();  // GLリソースの解放にはカレントなコンテキストが必要
    m_program.reset();
    if (m_vbo.isCreated()) m_vbo.destroy();
    doneCurrent();
}

void GLCanvas::setLayerStack(std::vector<StackEntry> stack, core::Bitmap* active, QPointF activeOffset) {
    m_layerStack = std::move(stack);
    m_bitmap = active;
    m_activeOffset = activeOffset;
    update();
}

void GLCanvas::setLayerStack(std::vector<const core::Bitmap*> stack, core::Bitmap* active) {
    std::vector<StackEntry> entries;
    entries.reserve(stack.size());
    for (const core::Bitmap* bitmap : stack) entries.push_back({bitmap, QPointF(), 1.0});
    setLayerStack(std::move(entries), active, QPointF());
}

void GLCanvas::setBitmap(core::Bitmap* bitmap) {
    setLayerStack(bitmap ? std::vector<const core::Bitmap*>{bitmap} : std::vector<const core::Bitmap*>{}, bitmap);
}

void GLCanvas::setOnionSkin(const core::Bitmap* prev, const core::Bitmap* next) {
    m_prevOnion = prev;
    m_nextOnion = next;
    update();
}

void GLCanvas::setLightTable(std::vector<const core::Bitmap*> bitmaps) {
    m_lightTable = std::move(bitmaps);
    update();
}

void GLCanvas::clearTextureCache() {
    makeCurrent();
    m_textures.clear();
    doneCurrent();
    m_pendingUploadBitmap = nullptr;  // 破棄されたBitmapへの転送予約も無効化
    m_pendingUploadRect = core::DirtyRect{};
    update();
}

void GLCanvas::notifyBitmapRegionChanged(core::Bitmap* bitmap, const core::DirtyRect& rect) {
    queueUpload(bitmap, rect);
    update();
}

void GLCanvas::setUnderlayImage(const QImage& image) {
    if (image.isNull()) {
        clearUnderlay();
        return;
    }

    // RGBA8888に統一して保持し、実際のテクスチャ生成/アップロードは次回paintGL冒頭まで遅延する。
    // ここでmakeCurrent()して即時アップロードすると、GLコンテキストを共有する兄弟GLCanvas
    // (内容欄/セリフ欄など)へ連続して呼んだ際に描画ループ外でのコンテキスト切り替えが競合し、
    // 後から呼んだ側のテクスチャが壊れる(黒くなる)ことがあるため、paintGL内(getOrCreateTexture
    // と同様に描画ループの中)でのみGL呼び出しを行うようにする。
    // QOpenGLTexture(QImage)コンストラクタは内部で上下反転してしまい、
    // 本プロジェクトのUV規約(行0=画像上端, v=0=上端)と食い違うため使用しない
    m_pendingUnderlayImage = image.convertToFormat(QImage::Format_RGBA8888);
    m_underlayImageDirty = true;
    m_underlayClearRequested = false;
    update();
}

void GLCanvas::clearUnderlay() {
    if (!m_underlayTexture && !m_underlayImageDirty) return;
    m_pendingUnderlayImage = QImage();
    m_underlayImageDirty = true;
    m_underlayClearRequested = true;
    update();
}

void GLCanvas::setUnderlayOpacity(float opacity01) {
    m_underlayOpacity = std::clamp(opacity01, 0.0f, 1.0f);
    update();
}

void GLCanvas::setOverlayImage(const QImage& image) {
    if (image.isNull()) {
        clearOverlay();
        return;
    }
    m_pendingOverlayImage = image.convertToFormat(QImage::Format_RGBA8888);
    m_overlayImageDirty = true;
    m_overlayClearRequested = false;
    update();
}

void GLCanvas::clearOverlay() {
    if (!m_overlayTexture && !m_overlayImageDirty) return;
    m_pendingOverlayImage = QImage();
    m_overlayImageDirty = true;
    m_overlayClearRequested = true;
    update();
}

void GLCanvas::setTool(Tool tool) {
    m_tool = tool;
    applyToolSettings();
    if (m_tool == Tool::Eyedropper) {
        setCursor(Qt::CrossCursor);
    } else if (!m_panning) {
        unsetCursor();
    }
}

void GLCanvas::applySettingsFor(Tool tool) {
    auto& settings = m_brush.settings();
    if (tool == Tool::Pen) {
        settings.radius = m_penRadius;
        settings.color = {static_cast<uint8_t>(m_penColor.red()), static_cast<uint8_t>(m_penColor.green()),
                           static_cast<uint8_t>(m_penColor.blue()), static_cast<uint8_t>(m_penColor.alpha())};
        settings.pressureAffectsRadius = true;
        settings.mode = core::BrushMode::Paint;
    } else if (tool == Tool::Eraser) {
        // 消しゴム: セルを透明に戻す。筆圧の影響は受けない
        settings.radius = m_eraserRadius;
        settings.pressureAffectsRadius = false;
        settings.mode = core::BrushMode::Erase;
    }
    // Fill: ブラシ設定は使わない(performFillがペン色を直接参照する)
    // Move: ブラシを使わない(ポインタ操作は位置キーの差分としてシグナルで伝える)
}

void GLCanvas::setPenRadius(float radius) {
    m_penRadius = radius;
    if (m_tool == Tool::Pen) applyToolSettings();
}

void GLCanvas::setPenColor(QColor color) {
    m_penColor = color;
    if (m_tool == Tool::Pen) applyToolSettings();
}

void GLCanvas::setEraserRadius(float radius) {
    m_eraserRadius = radius;
    if (m_tool == Tool::Eraser) applyToolSettings();
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
}

void GLCanvas::resizeGL(int w, int h) {
    Q_UNUSED(w);
    Q_UNUSED(h);
}

QOpenGLTexture* GLCanvas::getOrCreateTexture(const core::Bitmap* bitmap) {
    if (!bitmap || bitmap->isEmpty()) return nullptr;

    auto it = m_textures.find(bitmap);
    if (it != m_textures.end()) {
        // サイズ変更されていたら作り直す
        if (it->second->width() == bitmap->width() && it->second->height() == bitmap->height()) {
            return it->second.get();
        }
        m_textures.erase(it);
    }

    auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture->setSize(bitmap->width(), bitmap->height());
    texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Nearest);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, bitmap->data());

    QOpenGLTexture* raw = texture.get();
    m_textures.emplace(bitmap, std::move(texture));
    return raw;
}

void GLCanvas::queueUpload(core::Bitmap* bitmap, const core::DirtyRect& rect) {
    if (!bitmap || rect.isEmpty()) return;
    if (m_pendingUploadBitmap && m_pendingUploadBitmap != bitmap) {
        // 別Bitmapの予約が残っている場合は先に転送してから入れ替える(稀なケース)
        makeCurrent();
        flushPendingUpload();
        doneCurrent();
    }
    m_pendingUploadBitmap = bitmap;
    m_pendingUploadRect.unite(rect);
}

// コンテキストがカレントな状態で呼ぶこと。
// 入力イベントごとではなく描画フレームごとに1回だけ転送する(60fps対策)
void GLCanvas::flushPendingUpload() {
    if (!m_pendingUploadBitmap || m_pendingUploadRect.isEmpty()) {
        m_pendingUploadBitmap = nullptr;
        m_pendingUploadRect = core::DirtyRect{};
        return;
    }

    auto it = m_textures.find(m_pendingUploadBitmap);
    if (it != m_textures.end()) {
        // 部分領域を連続バッファへ詰め替えてからアップロードする
        const core::DirtyRect rect = m_pendingUploadRect;
        const core::Bitmap* bitmap = m_pendingUploadBitmap;
        const int w = rect.width();
        const int h = rect.height();
        m_uploadScratch.resize(static_cast<size_t>(w) * h * 4);
        const int stride = bitmap->width() * 4;
        const uint8_t* src = bitmap->data() + static_cast<size_t>(rect.y0) * stride + static_cast<size_t>(rect.x0) * 4;
        for (int row = 0; row < h; ++row) {
            std::copy(src + static_cast<size_t>(row) * stride,
                      src + static_cast<size_t>(row) * stride + static_cast<size_t>(w) * 4,
                      m_uploadScratch.begin() + static_cast<size_t>(row) * w * 4);
        }
        it->second->setData(rect.x0, rect.y0, 0, w, h, 1, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                            m_uploadScratch.data());
    }
    // キャッシュ未作成ならgetOrCreateTextureが全転送するため何もしなくてよい

    m_pendingUploadBitmap = nullptr;
    m_pendingUploadRect = core::DirtyRect{};
}

QTransform GLCanvas::viewTransform() const {
    if (m_canvasWidth <= 0 || m_canvasHeight <= 0) return {};

    // フィット対象の矩形: 通常はカメラフレーム(0,0,canvas)、引きセル編集中は作業領域まで広げる。
    // これによりフレーム外まで見渡して描ける
    const QRectF fitRect =
        m_workArea.isEmpty() ? QRectF(0, 0, m_canvasWidth, m_canvasHeight) : m_workArea;

    const qreal ww = width();
    const qreal wh = height();
    const qreal iw = fitRect.width();
    const qreal ih = fitRect.height();
    const qreal fitScale = qMin(ww / iw, wh / ih);
    const qreal scale = fitScale * m_zoom;

    // widget = 中心+パン → 反転 → 回転 → 拡縮 → フィット矩形の中心を原点へ、の順で画像座標に適用される
    QTransform t;
    t.translate(ww * 0.5 + m_panOffset.x(), wh * 0.5 + m_panOffset.y());
    if (m_mirrorView) t.scale(-1.0, 1.0);  // 左右反転表示(ミラーチェック)
    t.rotate(m_rotationDeg);
    t.scale(scale, scale);
    t.translate(-fitRect.center().x(), -fitRect.center().y());
    return t;
}

QPointF GLCanvas::widgetToImage(QPointF widgetPos) const {
    return viewTransform().inverted().map(widgetPos);
}

void GLCanvas::resetView() {
    m_zoom = 1.0f;
    m_rotationDeg = 0.0;
    m_panOffset = QPointF(0, 0);
    update();
}

void GLCanvas::zoomToCanvasRect(const QRectF& rectPx) {
    if (m_canvasWidth <= 0 || m_canvasHeight <= 0 || rectPx.isEmpty()) return;
    const qreal ww = width();
    const qreal wh = height();
    if (ww <= 0.0 || wh <= 0.0) return;

    m_rotationDeg = 0.0;  // 回転はリセットしてよい仕様(viewTransform()と整合させるため0固定)

    const qreal iw = m_canvasWidth;
    const qreal ih = m_canvasHeight;
    const qreal fitScale = qMin(ww / iw, wh / ih);
    if (fitScale <= 0.0) return;

    // 数%の余白を残して矩形がウィジェットに収まるスケールを求め、m_zoom(フィット基準倍率)へ変換する
    constexpr qreal kMarginFactor = 0.92;
    const qreal desiredScale = qMin(ww * kMarginFactor / rectPx.width(), wh * kMarginFactor / rectPx.height());
    m_zoom = std::clamp(static_cast<float>(desiredScale / fitScale), 0.05f, 32.0f);

    // viewTransform()と同じ式(画像中心を原点へ→拡縮→回転(0)→反転→ウィジェット中心+パン)で、
    // 矩形の中心がウィジェット中心に来るようパンを逆算する
    const qreal scale = fitScale * m_zoom;
    const QPointF imageCenter(iw * 0.5, ih * 0.5);
    QPointF scaled = (rectPx.center() - imageCenter) * scale;
    if (m_mirrorView) scaled.setX(-scaled.x());
    m_panOffset = -scaled;

    update();
}

void GLCanvas::paintGL() {
    const auto paintStart = std::chrono::steady_clock::now();

    flushPendingUpload();  // 溜めた部分転送をフレームごとに1回だけ実行(60fps対策)

    // 下敷き画像の反映待ちがあれば、ここ(paintGL内=コンテキストが確実にカレント)でテクスチャへ反映する
    if (m_underlayImageDirty) {
        if (m_underlayClearRequested || m_pendingUnderlayImage.isNull()) {
            m_underlayTexture.reset();
        } else {
            m_underlayTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
            m_underlayTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_underlayTexture->setSize(m_pendingUnderlayImage.width(), m_pendingUnderlayImage.height());
            m_underlayTexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            m_underlayTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_underlayTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_underlayTexture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, m_pendingUnderlayImage.constBits());
        }
        m_underlayImageDirty = false;
        m_underlayClearRequested = false;
        m_pendingUnderlayImage = QImage();
    }
    if (m_overlayImageDirty) {
        if (m_overlayClearRequested || m_pendingOverlayImage.isNull()) {
            m_overlayTexture.reset();
        } else {
            m_overlayTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
            m_overlayTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_overlayTexture->setSize(m_pendingOverlayImage.width(), m_pendingOverlayImage.height());
            m_overlayTexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            m_overlayTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_overlayTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
            m_overlayTexture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, m_pendingOverlayImage.constBits());
        }
        m_overlayImageDirty = false;
        m_overlayClearRequested = false;
        m_pendingOverlayImage = QImage();
    }

    glDisable(GL_BLEND);  // Qt側がブレンド有効のまま呼ぶことがあるため明示的に無効化
    glClear(GL_COLOR_BUFFER_BIT);

    // 描画時間の指数移動平均を更新して抜ける(60fps目標の常時計測)
    const auto recordTime = [this, paintStart] {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paintStart).count();
        m_paintMsEma = m_paintMsEma <= 0.0 ? ms : m_paintMsEma * 0.9 + ms * 0.1;
    };

    // 編集対象が空(割付なしのコマ等)でも紙と他セルは描く
    if (!m_program || m_canvasWidth <= 0 || m_canvasHeight <= 0) {
        recordTime();
        return;
    }

    const QTransform t = viewTransform();
    const qreal ww = width();
    const qreal wh = height();

    m_program->bind();
    m_vbo.bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    glActiveTexture(GL_TEXTURE0);
    m_program->setUniformValue("uTex", 0);
    m_program->setUniformValue("uOpacity", 1.0f);

    // 画像座標の矩形をビュー変換(ズーム/回転/パン込み)でNDCへ落とし、クアッドを描く。
    // テクスチャの行0=画像上端なので、上端頂点にv=0を割り当てる
    const auto toNdcX = [ww](qreal x) { return static_cast<float>(x / ww * 2.0 - 1.0); };
    const auto toNdcY = [wh](qreal y) { return static_cast<float>(1.0 - y / wh * 2.0); };
    const auto drawQuad = [&](const QRectF& imageRect) {
        const QPointF tl = t.map(imageRect.topLeft());
        const QPointF tr = t.map(imageRect.topRight());
        const QPointF bl = t.map(imageRect.bottomLeft());
        const QPointF br = t.map(imageRect.bottomRight());
        const float vertices[] = {
            toNdcX(tl.x()), toNdcY(tl.y()), 0.0f, 0.0f,  //
            toNdcX(tr.x()), toNdcY(tr.y()), 1.0f, 0.0f,  //
            toNdcX(bl.x()), toNdcY(bl.y()), 0.0f, 1.0f,  //
            toNdcX(br.x()), toNdcY(br.y()), 1.0f, 1.0f,  //
        };
        m_vbo.allocate(vertices, sizeof(vertices));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    const QRectF canvasRect(0, 0, m_canvasWidth, m_canvasHeight);
    // 紙(白)を塗る範囲: 通常はカメラフレーム、引きセル編集中は作業領域まで広げる
    const QRectF paperRect = m_workArea.isEmpty() ? canvasRect : m_workArea;
    // ビットマップ自身のサイズ+セルオフセットの矩形(引きセル=キャンバスより大きい紙にも対応)
    const auto bitmapRect = [](const core::Bitmap* bitmap, QPointF offset) {
        return QRectF(offset.x(), offset.y(), bitmap->width(), bitmap->height());
    };

    // 1. 紙(白): 紙の矩形を白で塗る
    m_program->setUniformValue("uSolidWhite", 1.0f);
    m_program->setUniformValue("uOnionStrength", 0.0f);
    m_program->setUniformValue("uUnderlayMix", 0.0f);
    m_program->setUniformValue("uUseSolidColor", 0.0f);  // 前フレームの状態が残っていても確実に無効化する
    drawQuad(paperRect);
    m_program->setUniformValue("uSolidWhite", 0.0f);

    // 2. レイヤースタックを下→上の順にアルファ合成(タップ/ペグ移動のオフセット付き)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const StackEntry& entry : m_layerStack) {
        QOpenGLTexture* tex = getOrCreateTexture(entry.bitmap);
        if (!tex) continue;
        m_program->setUniformValue("uOpacity", static_cast<float>(std::clamp(entry.opacity, 0.0, 1.0)));
        tex->bind();
        drawQuad(bitmapRect(entry.bitmap, entry.offset));
        tex->release();
    }
    m_program->setUniformValue("uOpacity", 1.0f);
    glDisable(GL_BLEND);

    // ライトテーブル(任意動画の透かし表示): オニオンと同じ乗算方式で青系固定色。
    // 編集対象セルの現在位置に合わせて表示する(タップ合わせ)
    if (!m_lightTable.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);

        m_program->setUniformValue("uTint", kLightTableTint);
        m_program->setUniformValue("uOnionStrength", kOnionStrength);
        m_program->setUniformValue("uUnderlayMix", 0.0f);
        for (const core::Bitmap* bitmap : m_lightTable) {
            QOpenGLTexture* tex = getOrCreateTexture(bitmap);
            if (!tex) continue;
            tex->bind();
            drawQuad(bitmapRect(bitmap, m_activeOffset));
            tex->release();
        }

        glDisable(GL_BLEND);
    }

    // オニオンスキン(乗算ブレンドで白地を無視して色線のみ重ねる)。位置はタップ合わせ
    if (m_prevOnion || m_nextOnion) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);

        const auto drawOnion = [&](const core::Bitmap* bitmap, const QVector3D& tint) {
            QOpenGLTexture* tex = getOrCreateTexture(bitmap);
            if (!tex) return;
            m_program->setUniformValue("uTint", tint);
            m_program->setUniformValue("uOnionStrength", kOnionStrength);
            m_program->setUniformValue("uUnderlayMix", 0.0f);
            tex->bind();
            drawQuad(bitmapRect(bitmap, m_activeOffset));
            tex->release();
        };
        if (m_prevOnion) drawOnion(m_prevOnion, kPrevTint);
        if (m_nextOnion) drawOnion(m_nextOnion, kNextTint);

        glDisable(GL_BLEND);
    }

    // 下敷き(参照画像/連番シーケンス): 白との混合を乗算ブレンドで重ね、透かして見せる。
    // キャンバス全面に引き伸ばして表示(アスペクト差は仕様として許容)
    if (m_underlayTexture) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);

        m_program->setUniformValue("uOnionStrength", 0.0f);
        m_program->setUniformValue("uUnderlayMix", m_underlayOpacity);
        m_underlayTexture->bind();
        drawQuad(canvasRect);
        m_underlayTexture->release();

        glDisable(GL_BLEND);
    }

    // レイアウト用フレーム枠ガイド: 作画フレーム(100%)/TVセーフ(約90%)/タイトルセーフ(約80%)を
    // 半透明の線で重ね表示する。全レイヤー・オニオン・下敷き等の最後(一番上)に描く。
    // ビュー変換に乗せるため、線はキャンバス座標(画像座標)で定義する
    if (m_overlayTexture) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_program->setUniformValue("uOnionStrength", 0.0f);
        m_program->setUniformValue("uUnderlayMix", 0.0f);
        m_program->setUniformValue("uUseSolidColor", 0.0f);
        m_program->setUniformValue("uOpacity", 1.0f);
        m_overlayTexture->bind();
        drawQuad(canvasRect);
        m_overlayTexture->release();

        glDisable(GL_BLEND);
    }

    if (m_frameGuidesEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_program->setUniformValue("uUseSolidColor", 1.0f);

        constexpr qreal kLineThickness = 2.0;  // キャンバス座標での線の太さ(px相当)。ズームに依存しない

        // scale(0〜1)で中央基準に縮小した矩形の外周を、太さkLineThicknessの4本の矩形で描く
        const auto drawGuideFrame = [&](qreal scale, const QVector4D& color) {
            const qreal w = m_canvasWidth * scale;
            const qreal h = m_canvasHeight * scale;
            const qreal x0 = (m_canvasWidth - w) * 0.5;
            const qreal y0 = (m_canvasHeight - h) * 0.5;
            m_program->setUniformValue("uSolidColor", color);
            drawQuad(QRectF(x0, y0, w, kLineThickness));                              // 上辺
            drawQuad(QRectF(x0, y0 + h - kLineThickness, w, kLineThickness));          // 下辺
            drawQuad(QRectF(x0, y0, kLineThickness, h));                              // 左辺
            drawQuad(QRectF(x0 + w - kLineThickness, y0, kLineThickness, h));          // 右辺
        };

        drawGuideFrame(1.0, kFrameGuideColor);       // 作画フレーム(100%)
        drawGuideFrame(0.9, kTvSafeGuideColor);       // TVセーフ/アクションセーフ(約90%)
        drawGuideFrame(0.8, kTitleSafeGuideColor);    // タイトルセーフ(約80%)

        m_program->setUniformValue("uUseSolidColor", 0.0f);
        glDisable(GL_BLEND);
    }

    // 作業領域(引きセル編集)モードでは、カメラフレーム(0,0,canvas)の外周を橙色の目安線で示す。
    // 背景を広い紙に描くとき「今どこがフレーム内か」が分かるようにするため
    if (!m_workArea.isEmpty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_program->setUniformValue("uUseSolidColor", 1.0f);
        m_program->setUniformValue("uSolidColor", QVector4D(1.0f, 0.6f, 0.15f, 0.9f));  // 橙

        constexpr qreal kFrameThickness = 3.0;  // キャンバス座標での線の太さ(px相当)
        drawQuad(QRectF(0, 0, m_canvasWidth, kFrameThickness));                                 // 上辺
        drawQuad(QRectF(0, m_canvasHeight - kFrameThickness, m_canvasWidth, kFrameThickness));   // 下辺
        drawQuad(QRectF(0, 0, kFrameThickness, m_canvasHeight));                                 // 左辺
        drawQuad(QRectF(m_canvasWidth - kFrameThickness, 0, kFrameThickness, m_canvasHeight));    // 右辺

        m_program->setUniformValue("uUseSolidColor", 0.0f);
        glDisable(GL_BLEND);
    }

    // カメラフレーム(画面に写る範囲)枠オーバーレイ: シアンの太さ4pxの枠を最後(一番上)に描く。
    // 矩形はキャンバス座標(画像座標)で指定される。空矩形なら非表示
    if (!m_cameraFrameOverlay.isEmpty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_program->setUniformValue("uUseSolidColor", 1.0f);
        m_program->setUniformValue("uSolidColor", kCameraFrameOverlayColor);

        constexpr qreal kOverlayThickness = 4.0;  // キャンバス座標での線の太さ(px相当)
        const QRectF r = m_cameraFrameOverlay;
        drawQuad(QRectF(r.left(), r.top(), r.width(), kOverlayThickness));                        // 上辺
        drawQuad(QRectF(r.left(), r.bottom() - kOverlayThickness, r.width(), kOverlayThickness));  // 下辺
        drawQuad(QRectF(r.left(), r.top(), kOverlayThickness, r.height()));                        // 左辺
        drawQuad(QRectF(r.right() - kOverlayThickness, r.top(), kOverlayThickness, r.height()));    // 右辺

        m_program->setUniformValue("uUseSolidColor", 0.0f);
        glDisable(GL_BLEND);
    }

    m_vbo.release();
    m_program->release();

    recordTime();
}

void GLCanvas::performFill(QPointF widgetPos) {
    const QPointF img = widgetToImage(widgetPos) - m_activeOffset;
    core::Bitmap before;
    if (m_strokeCommandSink) before = *m_bitmap;  // Undo用

    const core::Bitmap::Pixel color{static_cast<uint8_t>(m_penColor.red()), static_cast<uint8_t>(m_penColor.green()),
                                    static_cast<uint8_t>(m_penColor.blue()), 255};
    // 境界は専用リスト(非表示の色トレス線も含む)。未指定なら表示スタックのビットマップ群を使う
    std::vector<const core::Bitmap*> boundary = m_fillBoundary;
    if (boundary.empty()) {
        for (const StackEntry& entry : m_layerStack) boundary.push_back(entry.bitmap);
    }
    const auto dirty = core::floodFill(*m_bitmap, boundary, static_cast<int>(img.x()), static_cast<int>(img.y()), color);
    if (dirty.isEmpty()) return;

    if (m_strokeCommandSink) {
        auto beforeRegion = core::StrokeCommand::copyRegion(before, dirty);
        auto afterRegion = core::StrokeCommand::copyRegion(*m_bitmap, dirty);
        m_strokeCommandSink(
            std::make_unique<core::StrokeCommand>(m_bitmap, dirty, std::move(beforeRegion), std::move(afterRegion)));
    }
    queueUpload(m_bitmap, dirty);
    update();
}

bool GLCanvas::pickColor(QPointF widgetPos) {
    const QPointF imagePos = widgetToImage(widgetPos);
    const auto sampleBitmap = [imagePos](const core::Bitmap* bitmap, QPointF offset, double opacity, QColor* out) {
        if (!bitmap || bitmap->isEmpty() || opacity <= 0.0) return false;
        const QPointF local = imagePos - offset;
        const int x = static_cast<int>(std::floor(local.x()));
        const int y = static_cast<int>(std::floor(local.y()));
        if (x < 0 || y < 0 || x >= bitmap->width() || y >= bitmap->height()) return false;

        const core::Bitmap::Pixel px = bitmap->pixel(x, y);
        const int alpha = static_cast<int>(std::lround(px.a * std::clamp(opacity, 0.0, 1.0)));
        if (alpha <= 0) return false;
        *out = QColor(px.r, px.g, px.b, std::clamp(alpha, 0, 255));
        return true;
    };

    QColor picked;
    for (auto it = m_layerStack.rbegin(); it != m_layerStack.rend(); ++it) {
        if (sampleBitmap(it->bitmap, it->offset, it->opacity, &picked)) {
            emit colorPicked(picked);
            return true;
        }
    }
    if (sampleBitmap(m_bitmap, m_activeOffset, 1.0, &picked)) {
        emit colorPicked(picked);
        return true;
    }
    return false;
}

void GLCanvas::pointerBegin(QPointF widgetPos, float pressure) {
    if (!m_inputEnabled) return;
    if (m_tool == Tool::Eyedropper) {
        pickColor(widgetPos);
        return;
    }
    if (!m_bitmap) return;
    if (m_tool == Tool::Fill) {
        performFill(widgetPos);
        return;
    }
    if (m_tool == Tool::Move) {
        // タップ/ペグ移動: オフセット補正なしの画像座標を開始点として記録する(ストロークは行わない)
        m_moveStartImg = widgetToImage(widgetPos);
        m_movingCel = true;
        emit celMoveStarted();
        return;
    }
    // セルローカル座標へ(タップ移動中のセルにも正しい位置に描けるようオフセットを差し引く)
    const QPointF img = widgetToImage(widgetPos) - m_activeOffset;
    m_strokeActive = true;
    m_smoothedImagePos = img;  // 手ブレ補正の起点
    if (m_strokeCommandSink) m_strokeSnapshot = *m_bitmap;  // Undo用に開始時点を保存
    const auto dirty = m_brush.beginStroke(*m_bitmap, static_cast<float>(img.x()), static_cast<float>(img.y()), pressure);
    m_strokeDirty = dirty;
    queueUpload(m_bitmap, dirty);
    update();
}

void GLCanvas::pointerMove(QPointF widgetPos, float pressure) {
    if (m_movingCel) {
        // 開始点からのトータル差分を渡す(累積誤差を避けるため、毎回開始点からの差分を計算する)
        emit celMoveDelta(widgetToImage(widgetPos) - m_moveStartImg);
        return;
    }
    if (!m_bitmap || !m_strokeActive) return;
    QPointF img = widgetToImage(widgetPos) - m_activeOffset;

    // 手ブレ補正: 生のペン位置へ指数移動平均で追従させ、線を滑らかにする
    if (m_stabilizer > 0) {
        const qreal alpha = 1.0 - 0.92 * (m_stabilizer / 100.0);  // 0→即追従, 100→強い平滑化
        m_smoothedImagePos += (img - m_smoothedImagePos) * alpha;
        img = m_smoothedImagePos;
    }

    const auto dirty = m_brush.continueStroke(*m_bitmap, static_cast<float>(img.x()), static_cast<float>(img.y()), pressure);
    m_strokeDirty.unite(dirty);
    queueUpload(m_bitmap, dirty);
    update();
}

void GLCanvas::pointerEnd() {
    if (m_movingCel) {
        m_movingCel = false;
        emit celMoveFinished();
        return;
    }
    if (!m_strokeActive) return;
    m_strokeActive = false;
    m_brush.endStroke();

    if (m_strokeCommandSink && m_bitmap && !m_strokeDirty.isEmpty()) {
        auto before = core::StrokeCommand::copyRegion(m_strokeSnapshot, m_strokeDirty);
        auto after = core::StrokeCommand::copyRegion(*m_bitmap, m_strokeDirty);
        m_strokeCommandSink(
            std::make_unique<core::StrokeCommand>(m_bitmap, m_strokeDirty, std::move(before), std::move(after)));
    }
    m_strokeSnapshot = core::Bitmap();  // スナップショットのメモリを解放
    m_strokeDirty = core::DirtyRect{};
}

void GLCanvas::tabletEvent(QTabletEvent* event) {
    // ペンの後端(消しゴム側)で描いた場合は、UI上の選択ツールを変えずに消しゴムとして扱う
    const bool eraserPointer = event->pointerType() == QPointingDevice::PointerType::Eraser;
    applySettingsFor(eraserPointer ? Tool::Eraser : m_tool);

    // 筆圧検知off時はペン圧を無視して最大筆圧(1.0)で描く
    const float pressure = m_pressureEnabled ? static_cast<float>(event->pressure()) : 1.0f;
    switch (event->type()) {
        case QEvent::TabletPress:
            pointerBegin(event->position(), pressure);
            break;
        case QEvent::TabletMove:
            pointerMove(event->position(), pressure);
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
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->button() == Qt::LeftButton) pointerBegin(event->position(), 1.0f);
}

void GLCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        m_panOffset += event->position() - m_lastPanPos;
        m_lastPanPos = event->position();
        update();
        return;
    }
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->buttons() & Qt::LeftButton) pointerMove(event->position(), 1.0f);
}

void GLCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        if (m_tool == Tool::Eyedropper) {
            setCursor(Qt::CrossCursor);
        } else {
            unsetCursor();
        }
        return;
    }
    if (event->source() != Qt::MouseEventNotSynthesized) return;
    if (event->button() == Qt::LeftButton) pointerEnd();
}

void GLCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    // ペン/消しゴムツール時は、1回目のクリックで既に点が打たれているためストロークは開始せず、
    // シグナルのみ発火する(絵コンテの絵の枠拡大表示トグルなど、呼び出し側の後始末に委ねる)。
    // 他ツール(塗りつぶし/移動)時は基底クラスの標準挙動に任せる
    if (event->button() == Qt::LeftButton && event->source() == Qt::MouseEventNotSynthesized &&
        (m_tool == Tool::Pen || m_tool == Tool::Eraser)) {
        emit doubleClickedOnCanvas(widgetToImage(event->position()));
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void GLCanvas::wheelEvent(QWheelEvent* event) {
    if (!m_bitmap) return;

    // Alt押下時はQtが水平デルタとして届けることがあるため両方を見る
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (delta == 0) return;

    const QPointF cursor = event->position();
    const QPointF anchorImg = widgetToImage(cursor);  // カーソル下の画像座標を固定点にする

    if (event->modifiers() & Qt::AltModifier) {
        m_rotationDeg += (delta > 0) ? 15.0 : -15.0;  // 15度刻みで回転
    } else {
        const float factor = (delta > 0) ? 1.25f : 0.8f;
        m_zoom = std::clamp(m_zoom * factor, 0.05f, 32.0f);
    }

    // 固定点がカーソル位置に留まるようパンを補正する
    const QPointF moved = viewTransform().map(anchorImg);
    m_panOffset += cursor - moved;
    update();
    event->accept();
}

void GLCanvas::debugFillAt(QPointF widgetPos) {
    if (!m_bitmap) return;
    performFill(widgetPos);
}

void GLCanvas::debugSimulateMoveDrag(QPointF widgetDelta) {
    const Tool previousTool = m_tool;
    setTool(Tool::Move);
    const QPointF center(width() * 0.5, height() * 0.5);
    pointerBegin(center, 1.0f);
    pointerMove(center + widgetDelta, 1.0f);
    pointerEnd();
    setTool(previousTool);
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
