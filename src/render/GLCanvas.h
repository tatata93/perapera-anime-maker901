#pragma once

#include <QColor>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QTransform>
#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/BrushEngine.h"
#include "core/Command.h"

class QOpenGLShaderProgram;
class QOpenGLTexture;

// 作画キャンバス。core::Bitmapをテクスチャとして表示し、
// ペン(QTabletEvent / Windows Inkバックエンド)・マウスによる描画入力を受け付ける。
// オニオンスキン(前後フレームの色付き重ね表示)にも対応する。
class GLCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class Tool { Pen, Eraser, Fill, Move };

    explicit GLCanvas(QWidget* parent = nullptr);
    ~GLCanvas() override;

    // キャンバス(作画用紙)のサイズ。編集対象が空のコマでも紙を描くために独立して持つ
    void setCanvasSize(int width, int height) {
        m_canvasWidth = width;
        m_canvasHeight = height;
    }

    // スタック1件 = ビットマップ+セルのタップ/ペグ移動オフセット(画像座標px)
    struct StackEntry {
        const core::Bitmap* bitmap = nullptr;
        QPointF offset;
    };

    // レイヤースタック(下→上の描画順)と編集対象を設定する(所有権は持たない)。
    // 紙(白)はキャンバス側が背景として描画し、各セルは透明ビットマップとして重なる。
    // activeがnullptr(割付なしのコマ等)でも紙とスタックは描画される。
    // activeOffsetは編集対象セルの現在コマ位置(入力座標の補正とオニオン等の位置合わせに使う)
    void setLayerStack(std::vector<StackEntry> stack, core::Bitmap* active, QPointF activeOffset = {});
    // 互換用(オフセットなしの単純スタック)
    void setLayerStack(std::vector<const core::Bitmap*> stack, core::Bitmap* active);

    // 塗りつぶしの境界レイヤー群。表示スタックとは独立に指定でき、
    // 非表示の色トレス線レイヤーも境界として効かせられる。空なら表示スタックを使う
    void setFillBoundaryLayers(std::vector<const core::Bitmap*> boundary) {
        m_fillBoundary = std::move(boundary);
    }
    // 単一レイヤーの簡易版(スタック={bitmap}として扱う)
    void setBitmap(core::Bitmap* bitmap);
    // オニオンスキン対象(前/次フレーム)。nullptrで非表示
    void setOnionSkin(const core::Bitmap* prev, const core::Bitmap* next);
    // ライトテーブル(任意動画の透かし表示)。オニオンスキンと同じ乗算方式で、青系固定色に着色して重ねる。
    // 空で非表示
    void setLightTable(std::vector<const core::Bitmap*> bitmaps);
    // フレーム構造の変更(追加/削除)後に呼び、古いテクスチャを破棄する
    void clearTextureCache();
    // Undo/Redo等で外部からBitmapの一部が書き換えられた際に部分再アップロードを予約する
    void notifyBitmapRegionChanged(core::Bitmap* bitmap, const core::DirtyRect& rect);

    // 直近の描画時間(ms、指数移動平均)。60fps目標の常時計測用
    double paintMillis() const { return m_paintMsEma; }

    // 下敷き(参照画像/連番シーケンス)。現在フレームに薄く透かして重ね表示する。
    // セッション限定の参照であり、.ppamプロジェクトファイルには保存されない。
    // 実際のテクスチャ生成/アップロードは次回paintGL冒頭まで遅延される(makeCurrent()を描画ループ外
    // から呼ぶと、GLコンテキストを共有する兄弟GLCanvas同士で連続アップロードした際に破損するため)
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
    // 消しゴムツールの半径を設定する(ペンの設定には影響しない)。
    // 現在ツールが消しゴムの場合は即座に反映する
    void setEraserRadius(float radius);

    // ストローク完了時にUndo用コマンドを受け取るコールバック(MainWindowがCommandStackへ積む)
    using StrokeCommandSink = std::function<void(std::unique_ptr<core::Command>)>;
    void setStrokeCommandSink(StrokeCommandSink sink) { m_strokeCommandSink = std::move(sink); }

    // ビュー操作: ズーム倍率(フィット基準)・回転(度)・パン。リセットでフィット表示に戻る
    void resetView();
    float zoom() const { return m_zoom; }
    // 画像(キャンバス)座標の矩形がウィジェットにちょうど収まるようズーム/パンを設定する。
    // 回転は0にリセットする。少し余白を持たせて表示する(絵コンテの絵の枠拡大表示などに使う)
    void zoomToCanvasRect(const QRectF& rectPx);

    // 左右反転表示(ミラーチェック用)。表示のみ反転し、描画座標は正しく逆変換される
    void setMirrorView(bool mirrored) {
        m_mirrorView = mirrored;
        update();
    }

    // レイアウト用フレーム枠ガイド。有効時、作画フレーム(100%)/TVセーフ(約90%)/タイトルセーフ(約80%)
    // の3重の枠を全レイヤー・オニオン等の上に半透明で重ね表示する。セッション限定でppamには保存しない
    void setFrameGuides(bool enabled) {
        m_frameGuidesEnabled = enabled;
        update();
    }

    // カメラフレーム(画面に写る範囲)枠のオーバーレイ表示。矩形はキャンバス座標(画像座標px)。
    // 空のQRectFを渡すと非表示になる。セッション限定でppamには保存しない
    void setCameraFrameOverlay(const QRectF& rectPx) {
        m_cameraFrameOverlay = rectPx;
        update();
    }

    // 作業領域(ワークエリア)。引きセル(キャンバス=カメラフレームより大きい紙)を編集するとき、
    // ビューの表示範囲・白い紙・入力可能範囲をこの矩形(キャンバス座標px)まで広げる。
    // これによりカメラフレームの外側まで背景を描ける。有効時はカメラフレーム(0,0,canvas)の
    // 外周を目安線として重ね表示する。空のQRectFで通常(カメラフレーム=紙)に戻る。ppam非保存
    void setWorkArea(const QRectF& rectPx) {
        if (m_workArea == rectPx) return;
        m_workArea = rectPx;
        update();
    }

    // 手ブレ補正の強さ(0=なし〜100=最大)。ペン/消しゴムのストロークを平滑化する
    void setStabilizer(int strength) { m_stabilizer = std::clamp(strength, 0, 100); }

    // 筆圧検知のon/off。offのときはタブレットのペン圧を無視して常に最大筆圧(1.0)で描く
    // (筆圧非対応ペンや、線幅を一定にしたいときに使う)。マウス描画は元々1.0固定なので影響しない
    void setPressureEnabled(bool enabled) { m_pressureEnabled = enabled; }
    bool pressureEnabled() const { return m_pressureEnabled; }

    // 端から端まで筆圧を変えながら1ストローク描く(動作確認用フック)
    void debugSimulateStroke();
    // 指定ウィジェット座標を塗りつぶす(動作確認用フック)
    void debugFillAt(QPointF widgetPos);
    // 移動ツールでウィジェット座標のドラッグ(widgetDelta分)を再現する(動作確認用フック)。
    // 中央から開始し、元のツールへ戻して終了する
    void debugSimulateMoveDrag(QPointF widgetDelta);
    // ビュー状態を直接設定する(動作確認用フック)
    void debugSetView(float zoom, qreal rotationDeg, QPointF panOffset) {
        m_zoom = zoom;
        m_rotationDeg = rotationDeg;
        m_panOffset = panOffset;
        update();
    }

signals:
    // 移動ツール(タップ/ペグ移動)のドラッグ操作。MainWindowが位置キーへ反映する
    void celMoveStarted();
    void celMoveDelta(QPointF totalDeltaImage);  // ドラッグ開始点からの累積差分(画像座標px)
    void celMoveFinished();
    // ペン/消しゴムツール時にキャンバス上でダブルクリックされた(imagePosは画像座標)。
    // 絵コンテの絵の枠拡大表示トグルなどに使う。ダブルクリックはストロークを開始しない
    void doubleClickedOnCanvas(QPointF imagePos);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
    void flushPendingUpload();

    // 部分アップロードを予約する(実際の転送はpaintGL冒頭で1回だけ行う=60fps対策)
    void queueUpload(core::Bitmap* bitmap, const core::DirtyRect& rect);

    core::Bitmap* m_bitmap = nullptr;                 // 編集対象(アクティブレイヤーのセル)
    QPointF m_activeOffset;                           // 編集対象セルの現在コマ位置(タップ移動)
    std::vector<StackEntry> m_layerStack;             // 表示レイヤー(下→上、オフセット付き)
    std::vector<const core::Bitmap*> m_fillBoundary;  // 塗りつぶし境界(セルローカル座標)
    const core::Bitmap* m_prevOnion = nullptr;
    const core::Bitmap* m_nextOnion = nullptr;
    std::vector<const core::Bitmap*> m_lightTable;  // ライトテーブル表示対象(任意の動画群)

    core::BrushEngine m_brush;
    Tool m_tool = Tool::Pen;
    bool m_strokeActive = false;
    bool m_inputEnabled = true;

    // 移動ツール(タップ/ペグ移動)のドラッグ状態
    bool m_movingCel = false;
    QPointF m_moveStartImg;  // ドラッグ開始点の画像座標(オフセット補正なし)

    // ペンツールの半径・色(ツールバーのUIから変更される)
    float m_penRadius = 6.0f;
    QColor m_penColor = Qt::black;
    // 消しゴムツールの半径(ツールバーのUIから変更される)
    float m_eraserRadius = 24.0f;

    StrokeCommandSink m_strokeCommandSink;
    core::Bitmap m_strokeSnapshot;   // ストローク開始時点の全体コピー(Undo用)
    core::DirtyRect m_strokeDirty{};  // ストローク全体の書き換え矩形

    // キャンバスサイズ(作画用紙の画素数)
    int m_canvasWidth = 0;
    int m_canvasHeight = 0;

    // ビュー状態
    float m_zoom = 1.0f;         // フィット表示を1.0とする倍率
    qreal m_rotationDeg = 0.0;   // 時計回りの回転角(度)
    QPointF m_panOffset{0, 0};   // ウィジェット中心からのずれ(px)
    bool m_panning = false;
    bool m_mirrorView = false;   // 左右反転表示(ミラーチェック)
    QPointF m_lastPanPos;

    bool m_frameGuidesEnabled = false;  // レイアウト用フレーム枠ガイドの表示状態
    bool m_pressureEnabled = true;      // 筆圧検知(false=常に最大筆圧1.0で描く)
    QRectF m_cameraFrameOverlay;  // カメラフレーム枠オーバーレイ(キャンバス座標px、空=非表示)
    QRectF m_workArea;  // 作業領域(引きセル編集時にビュー/紙/入力を広げる、キャンバス座標px、空=カメラフレーム)

    // 手ブレ補正
    int m_stabilizer = 20;        // 強さ(0-100)
    QPointF m_smoothedImagePos;   // 平滑化されたペン位置(画像座標)

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unordered_map<const core::Bitmap*, std::unique_ptr<QOpenGLTexture>> m_textures;
    QOpenGLBuffer m_vbo;
    std::vector<uint8_t> m_uploadScratch;  // 部分アップロード用の連続バッファ

    // アップロード待ちの領域(paintGLで1回だけ転送する)
    core::Bitmap* m_pendingUploadBitmap = nullptr;
    core::DirtyRect m_pendingUploadRect{};

    double m_paintMsEma = 0.0;  // paintGL所要時間の指数移動平均(ms)

    // 下敷き(参照画像/連番シーケンス)用のテクスチャ。存在すれば現在フレームに薄く重ねる
    std::unique_ptr<QOpenGLTexture> m_underlayTexture;
    float m_underlayOpacity = 0.5f;
    // 下敷き画像の反映待ち状態(実アップロードはpaintGL冒頭で1回だけ行う)
    QImage m_pendingUnderlayImage;    // 反映待ちの画像(nullなら変更なし)
    bool m_underlayImageDirty = false;
    bool m_underlayClearRequested = false;  // trueならpaintGL冒頭でテクスチャを破棄する
};
