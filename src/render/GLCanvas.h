#pragma once

#include <QColor>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QTransform>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/BrushEngine.h"
#include "core/Command.h"

class QImage;
class QOpenGLShaderProgram;
class QOpenGLTexture;

// 作画キャンバス。core::Bitmapをテクスチャとして表示し、
// ペン(QTabletEvent / Windows Inkバックエンド)・マウスによる描画入力を受け付ける。
// オニオンスキン(前後フレームの色付き重ね表示)にも対応する。
class GLCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class Tool { Pen, Eraser, Fill };

    explicit GLCanvas(QWidget* parent = nullptr);
    ~GLCanvas() override;

    // レイヤースタック(下→上の描画順)と編集対象を設定する(所有権は持たない)。
    // 紙(白)はキャンバス側が背景として描画し、各セルは透明ビットマップとして重なる
    void setLayerStack(std::vector<const core::Bitmap*> stack, core::Bitmap* active);
    // 単一レイヤーの簡易版(スタック={bitmap}として扱う)
    void setBitmap(core::Bitmap* bitmap);
    // オニオンスキン対象(前/次フレーム)。nullptrで非表示
    void setOnionSkin(const core::Bitmap* prev, const core::Bitmap* next);
    // フレーム構造の変更(追加/削除)後に呼び、古いテクスチャを破棄する
    void clearTextureCache();

    // 下敷き(参照画像/連番シーケンス)。現在フレームに薄く透かして重ね表示する。
    // セッション限定の参照であり、.ppamプロジェクトファイルには保存されない
    void setUnderlayImage(const QImage& image);
    void clearUnderlay();
    void setUnderlayOpacity(float opacity01);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setInputEnabled(bool enabled) { m_inputEnabled = enabled; }

    // ペンツールの半径・色を設定する(消しゴムの設定には影響しない)。
    // 現在ツールがペンの場合は即座に反映する
    void setPenRadius(float radius);
    void setPenColor(QColor color);

    // ストローク完了時にUndo用コマンドを受け取るコールバック(MainWindowがCommandStackへ積む)
    using StrokeCommandSink = std::function<void(std::unique_ptr<core::Command>)>;
    void setStrokeCommandSink(StrokeCommandSink sink) { m_strokeCommandSink = std::move(sink); }

    // ビュー操作: ズーム倍率(フィット基準)・回転(度)・パン。リセットでフィット表示に戻る
    void resetView();
    float zoom() const { return m_zoom; }

    // 端から端まで筆圧を変えながら1ストローク描く(動作確認用フック)
    void debugSimulateStroke();
    // 指定ウィジェット座標を塗りつぶす(動作確認用フック)
    void debugFillAt(QPointF widgetPos);
    // ビュー状態を直接設定する(動作確認用フック)
    void debugSetView(float zoom, qreal rotationDeg, QPointF panOffset) {
        m_zoom = zoom;
        m_rotationDeg = rotationDeg;
        m_panOffset = panOffset;
        update();
    }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void pointerBegin(QPointF widgetPos, float pressure);
    void pointerMove(QPointF widgetPos, float pressure);
    void pointerEnd();
    void performFill(QPointF widgetPos);

    // 画像座標→ウィジェット座標の変換(フィット×ズーム×回転×パン)
    QTransform viewTransform() const;
    QPointF widgetToImage(QPointF widgetPos) const;
    void applyToolSettings() { applySettingsFor(m_tool); }
    void applySettingsFor(Tool tool);

    // コンテキストがカレントな状態で呼ぶこと
    QOpenGLTexture* getOrCreateTexture(const core::Bitmap* bitmap);
    void uploadDirty(const core::DirtyRect& rect);

    core::Bitmap* m_bitmap = nullptr;                 // 編集対象(アクティブレイヤーのセル)
    std::vector<const core::Bitmap*> m_layerStack;    // 表示レイヤー(下→上)
    const core::Bitmap* m_prevOnion = nullptr;
    const core::Bitmap* m_nextOnion = nullptr;

    core::BrushEngine m_brush;
    Tool m_tool = Tool::Pen;
    bool m_strokeActive = false;
    bool m_inputEnabled = true;

    // ペンツールの半径・色(ツールバーのUIから変更される)
    float m_penRadius = 6.0f;
    QColor m_penColor = Qt::black;

    StrokeCommandSink m_strokeCommandSink;
    core::Bitmap m_strokeSnapshot;   // ストローク開始時点の全体コピー(Undo用)
    core::DirtyRect m_strokeDirty{};  // ストローク全体の書き換え矩形

    // ビュー状態
    float m_zoom = 1.0f;         // フィット表示を1.0とする倍率
    qreal m_rotationDeg = 0.0;   // 時計回りの回転角(度)
    QPointF m_panOffset{0, 0};   // ウィジェット中心からのずれ(px)
    bool m_panning = false;
    QPointF m_lastPanPos;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unordered_map<const core::Bitmap*, std::unique_ptr<QOpenGLTexture>> m_textures;
    QOpenGLBuffer m_vbo;
    std::vector<uint8_t> m_uploadScratch;  // 部分アップロード用の連続バッファ

    // 下敷き(参照画像/連番シーケンス)用のテクスチャ。存在すれば現在フレームに薄く重ねる
    std::unique_ptr<QOpenGLTexture> m_underlayTexture;
    float m_underlayOpacity = 0.5f;
};
