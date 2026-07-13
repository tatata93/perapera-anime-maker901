#include "MainWindow.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QColorDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QRectF>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

#include "core/BrushEngine.h"
#include "core/Compositor.h"
#include "core/ProjectIO.h"
#include "core/StrokeCommand.h"
#include "previz/PrevizViewport.h"
#include "previz/PrevizWindow.h"
#include "render/GLCanvas.h"
#include "ui/CameraPanel.h"
#include "ui/CanvasSizeDialog.h"
#include "ui/CelPanel.h"
#include "ui/CelSizeDialog.h"
#include "ui/EditWindow.h"
#include "ui/ExportDialog.h"
#include "ui/FramePanel.h"
#include "ui/LayerPanel.h"
#include "ui/PalettePanel.h"
#include "ui/ReferencePanel.h"
#include "ui/SettingBoardWindow.h"
#include "ui/ShootingWindow.h"
#include "ui/StoryboardWindow.h"
#include "ui/TapPanel.h"
#include "ui/XsheetPanel.h"

namespace {
constexpr int kDefaultFps = 24;  // タイムシートは24fps基準

// 自動保存: フォルダ名と保存間隔(3分)
const QString kAutosaveFileName = QStringLiteral("autosave.ppproj");
constexpr int kAutosaveIntervalMs = 180 * 1000;

// 透明なセル(作画用紙)。紙の白はGLCanvasが背景として描画する。
// width/heightは呼び出し側でプロジェクトのキャンバスサイズ(MainWindow::canvasWidth/Height())を渡す
core::Bitmap makeTransparentCel(int width, int height) {
    core::Bitmap bitmap(width, height);
    bitmap.fill({0, 0, 0, 0});
    return bitmap;
}

// 引きセル対応: 指定セルの用紙サイズ(0ならキャンバスサイズ)で透明ビットマップを作る。
// 既存セルへ動画やレイヤーを追加する際、既にリサイズ済みの用紙サイズに合わせるために使う。
// canvasW/canvasHは呼び出し側の現在のキャンバスサイズ(セルのpaperサイズが0=既定のときに使う)
core::Bitmap makeTransparentCelForCel(const core::Cel& cel, int canvasW, int canvasH) {
    const int w = cel.paperWidth() > 0 ? cel.paperWidth() : canvasW;
    const int h = cel.paperHeight() > 0 ? cel.paperHeight() : canvasH;
    return makeTransparentCel(w, h);
}
// 新規カットの最小構成(セルA+レイヤー1+動画1、尺1コマ)を作る。canvasW/canvasHは新規セルのサイズに使う
void initializeCut(core::Cut& cut, int canvasW, int canvasH) {
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("レイヤー 1");
    layer.addFrame().bitmap() = makeTransparentCel(canvasW, canvasH);
    cut.setFrameCount(1);
    cel.setExposure(0, 0);
}

// プリビズなぞり作画用: 筆圧ペンで点列(ポリライン)をBitmapへ描く。debugSimulateStroke
// (GLCanvas)の筆圧の付け方に倣い、ストローク全体の弧長を媒介変数化して両端が細く・
// 中央が太くなる筆圧プロファイル(sinカーブ)にすることで、コードの均一な図形ではなく
// 「ペンで描いたような強弱のある線」にする。
void drawPenStroke(core::Bitmap& bitmap, const std::vector<QPointF>& pts, QColor color, float maxRadius) {
    if (pts.size() < 2) return;

    // 全体の弧長を求め、各頂点の弧長位置を弧長比0〜1へ正規化するための累積長を作る
    std::vector<double> cumLen(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        const QPointF d = pts[i] - pts[i - 1];
        cumLen[i] = cumLen[i - 1] + std::hypot(d.x(), d.y());
    }
    const double total = cumLen.back();
    if (total <= 0.0) return;

    constexpr double kPi = 3.14159265358979323846;
    // t(0〜1)に対する筆圧: 両端0.15(かすれ)、中央1.0(太い)のsinカーブ
    const auto pressureAt = [](double t) { return 0.15 + 0.85 * std::sin(kPi * t); };

    core::BrushEngine engine;
    engine.settings().radius = maxRadius;
    engine.settings().color = {static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
                               static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())};

    engine.beginStroke(bitmap, static_cast<float>(pts[0].x()), static_cast<float>(pts[0].y()),
                        static_cast<float>(pressureAt(0.0)));
    constexpr int kSubSteps = 24;  // 線分ごとの分割数(筆圧を滑らかに変化させるため)
    for (size_t i = 1; i < pts.size(); ++i) {
        for (int s = 1; s <= kSubSteps; ++s) {
            const double localT = static_cast<double>(s) / kSubSteps;
            const QPointF p = pts[i - 1] + (pts[i] - pts[i - 1]) * localT;
            const double lenAtP = cumLen[i - 1] + (cumLen[i] - cumLen[i - 1]) * localT;
            const double t = lenAtP / total;
            engine.continueStroke(bitmap, static_cast<float>(p.x()), static_cast<float>(p.y()),
                                   static_cast<float>(pressureAt(t)));
        }
    }
    engine.endStroke();
}

// プリビズカメラ画像(QImage)をキャンバスサイズへアスペクト維持でスケール(レターボックス)し、
// アルファを下げて淡い下敷き用Bitmapにする。renderCameraViewImage()はグリッド背景色で
// 全面塗りつぶされた不透明画像を返すため、アルファを一律に落とすだけで
// 「薄い3Dの下敷き」らしい見た目になる(白紙の上に重なって淡く見える)
core::Bitmap makeFaintPrevizBitmap(const QImage& src, int canvasWidth, int canvasHeight, float alphaScale) {
    core::Bitmap bitmap(canvasWidth, canvasHeight);
    bitmap.fill({0, 0, 0, 0});
    if (src.isNull()) return bitmap;

    const QImage scaled =
        src.scaled(canvasWidth, canvasHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGBA8888);
    const int offsetX = (canvasWidth - scaled.width()) / 2;
    const int offsetY = (canvasHeight - scaled.height()) / 2;
    const float clampedAlphaScale = std::clamp(alphaScale, 0.0f, 1.0f);

    for (int y = 0; y < scaled.height(); ++y) {
        const uchar* line = scaled.constScanLine(y);
        const int dy = offsetY + y;
        if (dy < 0 || dy >= canvasHeight) continue;
        for (int x = 0; x < scaled.width(); ++x) {
            const int dx = offsetX + x;
            if (dx < 0 || dx >= canvasWidth) continue;
            const uchar* px = line + static_cast<size_t>(x) * 4;
            const core::Bitmap::Pixel pixel{px[0], px[1], px[2],
                                            static_cast<uint8_t>(std::lround(px[3] * clampedAlphaScale))};
            bitmap.setPixel(dx, dy, pixel);
        }
    }
    return bitmap;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("perapera-anime-maker901");

    m_canvas = new GLCanvas(this);
    m_canvas->setCanvasSize(canvasWidth(), canvasHeight());
    setCentralWidget(m_canvas);
    m_canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command> command) {
        m_commands.push(std::move(command));  // pushは冪等なexecute(after画素の再書き込み)を伴う
        markCutDirty(activeCut());
        updateWindowTitle();
    });

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    // 自動保存: 3分間隔で、未保存の変更がある場合のみ実行する
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_dirty) performAutosave();
    });
    m_autosaveTimer->start(kAutosaveIntervalMs);

    setDockNestingEnabled(true);

    createNewDocument();
    setupPanels();
    setupMenus();
    setupToolBar();
    setupCutBar();
    updateFrameLabel();
    updateWindowTitle();

    // 描画時間の常時表示(60fps目標=16.6ms以内の監視用)
    auto* perfLabel = new QLabel(this);
    statusBar()->addPermanentWidget(perfLabel);
    auto* perfTimer = new QTimer(this);
    connect(perfTimer, &QTimer::timeout, this, [this, perfLabel] {
        const double ms = m_canvas->paintMillis();
        if (ms > 0.0) perfLabel->setText(QStringLiteral("描画 %1 ms ").arg(ms, 0, 'f', 1));
    });
    perfTimer->start(500);
}

MainWindow::~MainWindow() = default;

core::Cut& MainWindow::activeCut() {
    // シーンはMVPでは1つ固定。カットは複数対応(カットバーで切替)
    core::Scene& scene = m_project->scene(0);
    m_activeCut = std::min(m_activeCut, scene.cutCount() - 1);
    return scene.cut(m_activeCut);
}

core::Cel& MainWindow::activeCel() {
    core::Cut& cut = activeCut();
    m_activeCel = std::min(m_activeCel, cut.celCount() - 1);
    return cut.cel(m_activeCel);
}

core::Layer& MainWindow::activeLayer() {
    core::Cel& cel = activeCel();
    m_activeLayer = std::min(m_activeLayer, cel.layerCount() - 1);
    return cel.layer(m_activeLayer);
}

// 保存スコープ(部分保存)の管理: markXxxDirty系ヘルパーで変更箇所を記録する。
// いずれもm_dirty(表示用フラグ)もあわせて立てる
void MainWindow::markProjectDirty() {
    m_dirtyScope.project = true;
    m_dirty = true;
}

void MainWindow::markStoryboardDirty() {
    m_dirtyScope.storyboard = true;
    m_dirty = true;
}

void MainWindow::markBoardsDirty() {
    m_dirtyScope.boards = true;
    m_dirty = true;
}

void MainWindow::markCutDirty(core::Cut& cut) {
    // idが未採番(0)のカットは保存時に採番されるまでどのファイル名になるか定まらないため、
    // 安全側(全カット書き出し)に倒す
    if (cut.id() == 0) {
        m_dirtyScope.allCuts = true;
    } else {
        m_dirtyScope.cutIds.insert(cut.id());
    }
    m_dirty = true;
}

void MainWindow::markAllDirty() {
    m_dirtyScope.project = true;
    m_dirtyScope.storyboard = true;
    m_dirtyScope.boards = true;
    m_dirtyScope.allCuts = true;
    m_dirtyScope.cutIds.clear();  // allCuts=trueなので個別集合は不要
    m_dirty = true;
}

// 現在のコマ(シート位置)に露出表で割り付けられた動画を、セル×レイヤーの順で集めて渡す
void MainWindow::updateCanvasLayers() {
    core::Cut& cut = activeCut();
    std::vector<GLCanvas::StackEntry> stack;
    std::vector<const core::Bitmap*> fillBoundary;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        core::Cel& cel = cut.cel(ci);
        if (!cel.visible()) continue;
        const int drawing = cel.exposure(m_currentFrame);
        if (drawing < 0) continue;  // このコマにセルなし
        const bool isActiveCel = (ci == m_activeCel);

        // タップ/ペグ移動: このコマでのセル位置
        const core::Vec2 position = cel.positionAt(m_currentFrame);
        const QPointF offset(position.x, position.y);

        for (size_t li = 0; li < cel.layerCount(); ++li) {
            core::Layer& layer = cel.layer(li);
            if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;
            const core::Bitmap* bitmap = &layer.frame(static_cast<size_t>(drawing)).bitmap();

            // 仕上げ表示中は色トレス線・作監修正レイヤーを隠す(最終画プレビュー)
            const bool hiddenByCleanView =
                m_cleanView && (layer.role() == core::LayerRole::ColorTrace || layer.role() == core::LayerRole::Correction);
            if (layer.visible() && !hiddenByCleanView) {
                stack.push_back({bitmap, offset});
                fillBoundary.push_back(bitmap);  // 境界はセルローカル座標(同一セル内で整合)
            } else if (isActiveCel && layer.role() == core::LayerRole::ColorTrace) {
                // 塗分け線: アクティブセルの色トレス線は非表示でも塗りつぶし境界として効かせる
                fillBoundary.push_back(bitmap);
            }
        }
    }

    // 編集対象 = アクティブセルの現在コマに割り付けられた動画のアクティブレイヤー
    core::Bitmap* editTarget = nullptr;
    const int activeDrawing = activeCel().exposure(m_currentFrame);
    if (activeDrawing >= 0 && static_cast<size_t>(activeDrawing) < activeLayer().frameCount()) {
        editTarget = &activeLayer().frame(static_cast<size_t>(activeDrawing)).bitmap();
    }
    const core::Vec2 activePos = activeCel().positionAt(m_currentFrame);
    m_canvas->setFillBoundaryLayers(std::move(fillBoundary));
    m_canvas->setLayerStack(std::move(stack), editTarget, QPointF(activePos.x, activePos.y));

    // 引きセル(カメラフレームより大きい紙)を編集するときは、作業領域をその紙の範囲まで広げて
    // フレーム外まで見渡して描けるようにする。通常サイズのセル(タップ移動で一部が枠外にはみ出す
    // だけの場合を含む)では作業領域を無効(=カメラフレーム=紙)に戻す
    QRectF workArea;
    if (editTarget &&
        (editTarget->width() > canvasWidth() || editTarget->height() > canvasHeight())) {
        const QRectF frame(0, 0, canvasWidth(), canvasHeight());
        const QRectF celRect(activePos.x, activePos.y, editTarget->width(), editTarget->height());
        workArea = frame.united(celRect);
        const qreal margin = 0.02 * std::max(workArea.width(), workArea.height());  // 端を少し余白に
        workArea.adjust(-margin, -margin, margin, margin);
    }
    m_canvas->setWorkArea(workArea);
}

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("カット 1");
    initializeCut(cut, canvasWidth(), canvasHeight());

    m_currentFrame = 0;
    m_activeCut = 0;
    m_activeCel = 0;
    m_activeLayer = 0;
    // 新規プロジェクトのキャンバスサイズ(既定1920x1080)へ作画キャンバスを合わせる
    m_canvas->setCanvasSize(canvasWidth(), canvasHeight());
    updateCanvasLayers();
    updateOnionSkin();
}

void MainWindow::setCurrentFrame(size_t index) {
    core::Cut& cut = activeCut();
    if (cut.frameCount() == 0) return;
    m_currentFrame = std::min(index, cut.frameCount() - 1);
    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateUnderlay();
    updateXsheetPanel();  // 現在コマの行を追従させる
    if (m_previzWindow) m_previzWindow->setTimeline(m_currentFrame, cut.frameCount());  // プリビズもコマ連動
}

void MainWindow::addFrameAfterCurrent() {
    if (m_playing) return;
    // 新しい動画を作る(セル内全レイヤーに同時追加)。
    // 現在コマが空欄(未割付)ならその場に割り付け、コマは移動しない。
    // 既に割付済みなら次のコマに割り付け、次のコマが尺を超える場合は尺を伸ばす
    core::Cel& cel = activeCel();
    const int newDrawing = static_cast<int>(cel.drawingCount());
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        cel.layer(li).addFrame().bitmap() = makeTransparentCelForCel(cel, canvasWidth(), canvasHeight());
    }
    const bool currentIsEmpty = cel.exposure(m_currentFrame) == -1;
    const size_t target = currentIsEmpty ? m_currentFrame : m_currentFrame + 1;
    core::Cut& cut = activeCut();
    if (target >= cut.frameCount()) cut.setFrameCount(target + 1);
    cel.setExposure(target, newDrawing);

    m_commands.clear();             // 構造変更のためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 動画構造が変わったため
    setCurrentFrame(target);
    markCutDirty(cut);
    updateWindowTitle();
}

void MainWindow::deleteCurrentFrame() {
    if (m_playing) return;
    // 現在コマの割付を空にする(動画自体は残る=シート上のセル欄を消す操作)
    activeCel().setExposure(m_currentFrame, -1);
    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateXsheetPanel();
    markCutDirty(activeCut());
    updateWindowTitle();
}

// コマ打ちパターン適用(1/2/3コマ打ち)。XsheetPanelのボタンと「操作」メニューの数字キーで共有する
void MainWindow::applyStepPattern(int step) {
    if (m_playing) return;
    activeCel().applyStepPattern(step, activeCut().frameCount());
    markCutDirty(activeCut());
    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateXsheetPanel();
    updateWindowTitle();
}

void MainWindow::togglePlayback() {
    m_playing = !m_playing;
    if (m_playing) {
        m_playAction->setText(tr("停止 (Space)"));
        m_canvas->setInputEnabled(false);
        updateOnionSkin();          // 再生中はオニオンスキンを消す
        m_canvas->setLightTable({});  // 再生中はライトテーブルも消す
        m_playTimer->start(1000 / std::max(1, m_fpsSpin->value()));
    } else {
        m_playTimer->stop();
        m_playAction->setText(tr("再生 (Space)"));
        m_canvas->setInputEnabled(true);
        updateOnionSkin();
        updateLightTable();  // 再生停止でライトテーブルを復元する
    }
}

void MainWindow::onPlaybackTick() {
    const size_t count = activeCut().frameCount();
    if (count == 0) return;
    setCurrentFrame((m_currentFrame + 1) % count);
}

void MainWindow::updateOnionSkin() {
    if (!m_onionEnabled || m_playing) {
        m_canvas->setOnionSkin(nullptr, nullptr);
        return;
    }
    // 前後の「異なる動画」を探して表示する(2コマ打ち等で同じ絵が続く区間はスキップ)
    core::Cel& cel = activeCel();
    core::Layer& layer = activeLayer();
    const core::Cut& cut = activeCut();
    const int current = cel.exposure(m_currentFrame);

    const auto drawingBitmap = [&layer](int drawing) -> const core::Bitmap* {
        if (drawing < 0 || static_cast<size_t>(drawing) >= layer.frameCount()) return nullptr;
        return &layer.frame(static_cast<size_t>(drawing)).bitmap();
    };

    const core::Bitmap* prev = nullptr;
    for (size_t t = m_currentFrame; t > 0; --t) {
        const int e = cel.exposure(t - 1);
        if (e >= 0 && e != current) {
            prev = drawingBitmap(e);
            break;
        }
    }
    const core::Bitmap* next = nullptr;
    for (size_t t = m_currentFrame + 1; t < cut.frameCount(); ++t) {
        const int e = cel.exposure(t);
        if (e >= 0 && e != current) {
            next = drawingBitmap(e);
            break;
        }
    }
    m_canvas->setOnionSkin(prev, next);
}

// 動画インデックス一覧→アクティブレイヤーの該当frame(i).bitmap()を集める。
// 現在コマに割付中の動画は重複表示を避けるため除外する
std::vector<const core::Bitmap*> MainWindow::collectLightTableBitmaps(const QList<int>& drawings) {
    core::Cel& cel = activeCel();
    core::Layer& layer = activeLayer();
    const int current = cel.exposure(m_currentFrame);

    std::vector<const core::Bitmap*> bitmaps;
    for (int drawing : drawings) {
        if (drawing < 0 || static_cast<size_t>(drawing) >= layer.frameCount()) continue;
        if (drawing == current) continue;
        bitmaps.push_back(&layer.frame(static_cast<size_t>(drawing)).bitmap());
    }
    return bitmaps;
}

void MainWindow::updateLightTable() {
    if (m_playing) return;  // 再生中はtogglePlaybackが明示的に消すのでここでは触らない
    if (!m_framePanel) {
        m_canvas->setLightTable({});
        return;
    }
    m_canvas->setLightTable(collectLightTableBitmaps(m_framePanel->lightTableDrawings()));
}

void MainWindow::debugSetLightTable(const QList<int>& drawings) {
    m_canvas->setLightTable(collectLightTableBitmaps(drawings));
}

void MainWindow::debugSetOnionEnabled(bool enabled) {
    m_onionEnabled = enabled;
    if (m_onionAction) m_onionAction->setChecked(enabled);
    updateOnionSkin();
}

void MainWindow::updateFrameLabel() {
    if (m_frameLabel) {
        m_frameLabel->setText(
            QStringLiteral(" コマ %1 / %2 ").arg(m_currentFrame + 1).arg(activeCut().frameCount()));
    }
    if (m_framePanel) {
        core::Cel& cel = activeCel();
        const int drawingCount = static_cast<int>(cel.drawingCount());

        QList<int> displayOrder;
        displayOrder.reserve(drawingCount);
        if (m_framePanel->sortMode() == 1) {
            // 再生順: 露出表を先頭から走査し、動画の初出順に並べる
            QList<bool> used;
            used.fill(false, drawingCount);
            for (size_t f = 0; f < activeCut().frameCount(); ++f) {
                const int drawing = cel.exposure(f);
                if (drawing < 0 || drawing >= drawingCount || used[drawing]) continue;
                used[drawing] = true;
                displayOrder.append(drawing);
            }
            // シートに登場しない動画は末尾に番号順で追加する
            for (int d = 0; d < drawingCount; ++d) {
                if (!used[d]) displayOrder.append(d);
            }
        } else {
            // 番号順
            for (int d = 0; d < drawingCount; ++d) displayOrder.append(d);
        }

        // 動画(絵)一覧。選択=現在コマに割り付けられた動画
        m_framePanel->setDrawings(displayOrder, cel.exposure(m_currentFrame));
        m_framePanel->setWindowTitle(tr("動画 - セル %1").arg(QString::fromStdString(cel.name())));
    }
    // 動画一覧(チェック状態)の再構築後にライトテーブルも追従させる(構造変更後の破棄漏れ防止)
    updateLightTable();
}

void MainWindow::openUnderlay() {
    const QString path = QFileDialog::getOpenFileName(this, tr("下敷き画像/連番を開く"), QString(),
                                                        tr("画像ファイル (*.png *.jpg *.jpeg *.bmp)"));
    if (!path.isEmpty()) setUnderlaySequenceFromFile(path);
}

void MainWindow::setUnderlaySequenceFromFile(const QString& path) {
    // 選択ファイルと同じフォルダ内の同拡張子ファイルを名前順に集めて連番として扱う。
    // 該当ファイルが1枚だけなら静止画扱い(全フレームで同じ画像を表示)になる
    const QFileInfo info(path);
    const QDir dir = info.dir();
    const QStringList filters = {QStringLiteral("*.%1").arg(info.suffix())};
    const QStringList names = dir.entryList(filters, QDir::Files, QDir::Name);

    m_underlaySequence.clear();
    for (const QString& name : names) {
        m_underlaySequence.append(dir.filePath(name));
    }
    m_underlayLoadedIndex = -1;  // フォルダが変わったので必ず再ロードさせる
    updateUnderlay();
}

void MainWindow::clearUnderlaySequence() {
    m_underlaySequence.clear();
    m_underlayLoadedIndex = -1;
    m_canvas->clearUnderlay();
}

void MainWindow::updateUnderlay() {
    // プリビズ下敷き: プリビズカメラの現在コマの絵をそのまま透かす(なぞり作画)
    if (m_previzUnderlay && m_previzWindow) {
        m_previzWindow->setFrame(m_currentFrame);
        m_canvas->setUnderlayImage(
            m_previzWindow->viewport()->renderCameraViewImage(static_cast<float>(canvasWidth()) / canvasHeight()));
        return;
    }

    if (m_underlaySequence.isEmpty()) return;

    const int lastIndex = static_cast<int>(m_underlaySequence.size()) - 1;
    const int index = std::min(static_cast<int>(m_currentFrame), lastIndex);
    if (index == m_underlayLoadedIndex) return;  // 直前と同じファイルなら再ロードしない

    m_underlayLoadedIndex = index;
    const QImage image(m_underlaySequence.at(index));
    if (!image.isNull()) m_canvas->setUnderlayImage(image);
}

void MainWindow::debugSetUnderlayFile(const QString& path) {
    setUnderlaySequenceFromFile(path);
}

void MainWindow::choosePenColor() {
    const QColor chosen = QColorDialog::getColor(m_penColor, this, tr("ペンの色を選択"));
    if (!chosen.isValid()) return;  // キャンセル時は何もしない
    m_penColor = chosen;
    m_canvas->setPenColor(m_penColor);
    updatePenColorButton();
}

void MainWindow::updatePenColorButton() {
    if (!m_penColorButton) return;
    m_penColorButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #444;").arg(m_penColor.name()));
}

void MainWindow::setupCutBar() {
    QToolBar* cutBar = addToolBar(tr("カット"));
    cutBar->setMovable(false);

    cutBar->addWidget(new QLabel(tr(" カット: "), this));
    m_cutCombo = new QComboBox(this);
    m_cutCombo->setMinimumWidth(140);
    m_cutCombo->setFocusPolicy(Qt::ClickFocus);
    connect(m_cutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) setActiveCut(index);
    });
    cutBar->addWidget(m_cutCombo);

    QAction* addAction = cutBar->addAction(tr("カット追加"));
    connect(addAction, &QAction::triggered, this, &MainWindow::addCut);
    QAction* removeAction = cutBar->addAction(tr("カット削除"));
    connect(removeAction, &QAction::triggered, this, &MainWindow::removeActiveCut);
    QAction* renameAction = cutBar->addAction(tr("カット名変更"));
    connect(renameAction, &QAction::triggered, this, &MainWindow::renameActiveCut);

    updateCutBar();
}

void MainWindow::updateCutBar() {
    if (!m_cutCombo) return;
    const QSignalBlocker blocker(m_cutCombo);  // 再構築でsetActiveCutが暴発しないように
    m_cutCombo->clear();
    core::Scene& scene = m_project->scene(0);
    for (size_t i = 0; i < scene.cutCount(); ++i) {
        m_cutCombo->addItem(QString::fromStdString(scene.cut(i).name()));
    }
    m_cutCombo->setCurrentIndex(static_cast<int>(m_activeCut));
}

// カット切替: 編集位置をリセットし、全パネル・プリビズを新カットへ追従させる
void MainWindow::setActiveCut(int index) {
    if (m_playing) togglePlayback();
    core::Scene& scene = m_project->scene(0);
    if (index < 0 || static_cast<size_t>(index) >= scene.cutCount()) return;
    m_activeCut = static_cast<size_t>(index);
    m_currentFrame = 0;
    m_activeCel = 0;
    m_activeLayer = 0;
    m_commands.clear();             // 別カットのBitmapを参照するUndoを破棄
    m_canvas->clearTextureCache();
    if (m_previzWindow) {
        m_previzWindow->setScene(&activeCut().previz());
        m_previzWindow->setTimeline(m_currentFrame, activeCut().frameCount());
    }
    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateLayerPanel();
    updateXsheetPanel();
    updateUnderlay();
    updateCutBar();
}

void MainWindow::addCut() {
    if (m_playing) return;
    core::Scene& scene = m_project->scene(0);
    core::Cut& cut = scene.addCut(tr("カット %1").arg(scene.cutCount() + 1).toStdString());
    initializeCut(cut, canvasWidth(), canvasHeight());
    markProjectDirty();  // シーンのカット構成(cutIds)が変わる
    markCutDirty(cut);
    updateWindowTitle();
    setActiveCut(static_cast<int>(scene.cutCount() - 1));
    refreshEditWindowIfOpen();
}

void MainWindow::removeActiveCut() {
    if (m_playing) return;
    core::Scene& scene = m_project->scene(0);
    if (scene.cutCount() <= 1) return;  // 最後の1カットは消さない
    scene.removeCut(m_activeCut);
    markProjectDirty();  // シーンのカット構成(cutIds)が変わる。カット自体は削除済みなのでmarkCutDirtyは不要
    updateWindowTitle();
    setActiveCut(static_cast<int>(std::min(m_activeCut, scene.cutCount() - 1)));
    refreshEditWindowIfOpen();
}

void MainWindow::renameActiveCut() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("カット名変更"), tr("カット名:"), QLineEdit::Normal,
                                               QString::fromStdString(activeCut().name()), &ok);
    if (!ok || name.isEmpty()) return;
    activeCut().setName(name.toStdString());
    markProjectDirty();
    markCutDirty(activeCut());
    updateWindowTitle();
    updateCutBar();
    refreshEditWindowIfOpen();
}

void MainWindow::setupPanels() {
    m_framePanel = new FramePanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_framePanel);
    connect(m_framePanel, &FramePanel::frameSelected, this, [this](int index) {
        // 動画一覧のクリック = 現在コマにその動画を割り付ける(タイムシート編集)
        if (m_playing) return;
        activeCel().setExposure(m_currentFrame, index);
        markCutDirty(activeCut());
        updateCanvasLayers();
        updateOnionSkin();
        updateWindowTitle();
    });
    connect(m_framePanel, &FramePanel::addRequested, this, &MainWindow::addFrameAfterCurrent);
    connect(m_framePanel, &FramePanel::duplicateRequested, this, &MainWindow::duplicateDrawing);
    connect(m_framePanel, &FramePanel::deleteRequested, this, &MainWindow::deleteDrawing);
    connect(m_framePanel, &FramePanel::sortModeChanged, this, [this] { updateFrameLabel(); });
    connect(m_framePanel, &FramePanel::lightTableChanged, this, &MainWindow::updateLightTable);

    m_layerPanel = new LayerPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_layerPanel);
    connect(m_layerPanel, &LayerPanel::layerSelected, this, [this](int index) {
        if (m_playing) return;
        m_activeLayer = static_cast<size_t>(index);
        updateCanvasLayers();
        updateOnionSkin();
    });
    connect(m_layerPanel, &LayerPanel::visibilityChanged, this, [this](int index, bool visible) {
        core::Cel& cel = activeCel();
        if (static_cast<size_t>(index) >= cel.layerCount()) return;
        cel.layer(static_cast<size_t>(index)).setVisible(visible);
        updateCanvasLayers();
        markCutDirty(activeCut());
        updateWindowTitle();
    });
    connect(m_layerPanel, &LayerPanel::addRequested, this, &MainWindow::addLayerToActiveCel);
    connect(m_layerPanel, &LayerPanel::removeRequested, this, &MainWindow::removeActiveLayer);
    connect(m_layerPanel, &LayerPanel::moveUpRequested, this, [this] { moveActiveLayer(+1); });
    connect(m_layerPanel, &LayerPanel::moveDownRequested, this, [this] { moveActiveLayer(-1); });
    connect(m_layerPanel, &LayerPanel::renameRequested, this, [this](int index) {
        // レイヤー名のダブルクリックによる変更(現在名を初期値にし、空欄ならキャンセル扱い)
        core::Cel& cel = activeCel();
        if (static_cast<size_t>(index) >= cel.layerCount()) return;
        core::Layer& layer = cel.layer(static_cast<size_t>(index));

        bool ok = false;
        const QString newName = QInputDialog::getText(this, tr("レイヤー名を変更"), tr("レイヤー名:"),
                                                        QLineEdit::Normal, QString::fromStdString(layer.name()), &ok);
        if (!ok || newName.isEmpty()) return;  // キャンセルまたは空欄は変更しない
        layer.setName(newName.toStdString());

        markCutDirty(activeCut());
        updateWindowTitle();
        updateLayerPanel();
    });
    connect(m_layerPanel, &LayerPanel::roleChangeRequested, this, [this](int index, int role) {
        core::Cel& cel = activeCel();
        if (static_cast<size_t>(index) >= cel.layerCount()) return;
        core::LayerRole newRole = core::LayerRole::Normal;
        if (role == 1) {
            newRole = core::LayerRole::ColorTrace;
        } else if (role == 2) {
            newRole = core::LayerRole::Correction;
        }
        cel.layer(static_cast<size_t>(index)).setRole(newRole);
        markCutDirty(activeCut());
        updateWindowTitle();
        updateLayerPanel();
    });

    m_palettePanel = new PalettePanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_palettePanel);
    connect(m_palettePanel, &PalettePanel::colorSelected, this, [this](QColor color) {
        m_penColor = color;
        m_canvas->setPenColor(m_penColor);
        updatePenColorButton();
    });
    connect(m_palettePanel, &PalettePanel::addCurrentColorRequested, this, &MainWindow::addCurrentColorToPalette);
    connect(m_palettePanel, &PalettePanel::removeSelectedRequested, this, &MainWindow::removeSelectedPaletteColor);

    m_xsheetPanel = new XsheetPanel(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_xsheetPanel);
    connect(m_xsheetPanel, &XsheetPanel::cellClicked, this, [this](int celIndex, int frame) {
        if (m_playing) return;
        core::Cut& cut = activeCut();
        if (cut.celCount() == 0 || celIndex < 0 || frame < 0) return;
        const size_t newActiveCel = static_cast<size_t>(std::min(celIndex, static_cast<int>(cut.celCount()) - 1));
        const bool celChanged = newActiveCel != m_activeCel;
        m_activeCel = newActiveCel;
        setCurrentFrame(static_cast<size_t>(frame));
        if (celChanged) updateLayerPanel();  // アクティブセルが変わったのでレイヤーパネルも追従させる
    });
    connect(m_xsheetPanel, &XsheetPanel::exposureEdited, this, [this](int celIndex, int frame, int drawing) {
        if (m_playing) return;
        core::Cut& cut = activeCut();
        if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;
        if (frame < 0 || static_cast<size_t>(frame) >= cut.frameCount()) return;
        core::Cel& cel = cut.cel(static_cast<size_t>(celIndex));
        int clampedDrawing = drawing;
        if (clampedDrawing >= 0) {
            // 動画番号として無効な大きい値はここでクランプする(セルに動画が1枚もなければ空欄扱い)
            clampedDrawing = cel.drawingCount() == 0
                                 ? -1
                                 : std::min(clampedDrawing, static_cast<int>(cel.drawingCount()) - 1);
        }
        cel.setExposure(static_cast<size_t>(frame), clampedDrawing);
        markCutDirty(cut);
        updateCanvasLayers();
        updateOnionSkin();
        updateFrameLabel();
        updateXsheetPanel();
        updateWindowTitle();
    });
    connect(m_xsheetPanel, &XsheetPanel::frameCountChanged, this, [this](int frameCount) {
        if (m_playing || frameCount < 1) return;
        core::Cut& cut = activeCut();
        cut.setFrameCount(static_cast<size_t>(frameCount));
        if (m_currentFrame >= cut.frameCount()) m_currentFrame = cut.frameCount() - 1;
        markCutDirty(cut);
        updateCanvasLayers();
        updateOnionSkin();
        updateFrameLabel();
        updateXsheetPanel();
        updateWindowTitle();
    });
    connect(m_xsheetPanel, &XsheetPanel::stepPatternRequested, this, &MainWindow::applyStepPattern);
    // 動画追加/削除: FramePanel(動画パネル)の動画追加/動画削除ボタンと同じ処理を呼ぶ。
    // 削除は動画パネルと違い一覧選択がないため、現在コマに割り付いている動画を対象にする
    connect(m_xsheetPanel, &XsheetPanel::addDrawingRequested, this, &MainWindow::addFrameAfterCurrent);
    connect(m_xsheetPanel, &XsheetPanel::deleteDrawingRequested, this, [this] {
        if (m_playing) return;
        const int idx = activeCel().exposure(m_currentFrame);
        if (idx < 0) return;  // 現在コマが空欄なら対象なし
        deleteDrawing(idx);
    });
    connect(m_xsheetPanel, &XsheetPanel::celAddRequested, this, &MainWindow::addCel);
    connect(m_xsheetPanel, &XsheetPanel::celRemoveRequested, this, &MainWindow::removeActiveCel);
    connect(m_xsheetPanel, &XsheetPanel::celRenameRequested, this, &MainWindow::renameActiveCel);
    connect(m_xsheetPanel, &XsheetPanel::celMoveRequested, this, &MainWindow::moveActiveCel);
    connect(m_xsheetPanel, &XsheetPanel::celVisibilityToggleRequested, this, [this](int celIndex) {
        core::Cut& cut = activeCut();
        if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;
        setCelVisibility(celIndex, !cut.cel(static_cast<size_t>(celIndex)).visible());
    });

    m_celPanel = new CelPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_celPanel);
    connect(m_celPanel, &CelPanel::celSelected, this, [this](int index) {
        if (m_playing) return;
        setActiveCel(index);
    });
    connect(m_celPanel, &CelPanel::visibilityChanged, this, [this](int index, bool visible) {
        setCelVisibility(index, visible);
    });
    connect(m_celPanel, &CelPanel::celSizeRequested, this, &MainWindow::openCelSizeDialog);

    m_tapPanel = new TapPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_tapPanel);
    connect(m_tapPanel, &TapPanel::keySelected, this, [this](int frame) {
        if (m_playing) return;
        setCurrentFrame(static_cast<size_t>(frame));
    });
    connect(m_tapPanel, &TapPanel::addKeyRequested, this, [this] {
        if (m_playing) return;
        core::Cel& cel = activeCel();
        cel.setPositionKey(m_currentFrame, cel.positionAt(m_currentFrame));  // 現在の補間位置でキーを打つ
        markCutDirty(activeCut());
        updateCanvasLayers();
        updateWindowTitle();
        updateTapPanel();
    });
    connect(m_tapPanel, &TapPanel::removeKeyRequested, this, [this](int frame) {
        if (m_playing) return;
        activeCel().removePositionKey(static_cast<size_t>(frame));
        markCutDirty(activeCut());
        updateCanvasLayers();
        updateWindowTitle();
        updateTapPanel();
    });

    // 移動ツール(タップ/ペグ移動): ドラッグ開始時のセル位置を保存し、差分を位置キーへ反映する
    connect(m_canvas, &GLCanvas::celMoveStarted, this, [this] {
        m_moveBase = QPointF(activeCel().positionAt(m_currentFrame).x, activeCel().positionAt(m_currentFrame).y);
    });
    connect(m_canvas, &GLCanvas::celMoveDelta, this, [this](QPointF delta) {
        activeCel().setPositionKey(m_currentFrame,
                                    core::Vec2{static_cast<float>(m_moveBase.x() + delta.x()),
                                               static_cast<float>(m_moveBase.y() + delta.y())});
        updateCanvasLayers();  // リアルタイム追従
    });
    connect(m_canvas, &GLCanvas::celMoveFinished, this, [this] {
        markCutDirty(activeCut());
        updateWindowTitle();
        updateTapPanel();
    });

    m_cameraPanel = new CameraPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_cameraPanel);
    connect(m_cameraPanel, &CameraPanel::valuesChanged, this, [this](double, double, double) {
        updateCameraOverlay();  // データは変更せずプレビューのみ更新する
    });
    connect(m_cameraPanel, &CameraPanel::addKeyRequested, this, [this] {
        if (m_playing) return;
        core::CameraFrameState state;
        state.center = {static_cast<float>(m_cameraPanel->centerX()), static_cast<float>(m_cameraPanel->centerY())};
        state.scale = m_cameraPanel->scalePercent() / 100.0;
        activeCut().setCameraKey(m_currentFrame, state);
        markCutDirty(activeCut());
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::removeKeyRequested, this, [this] {
        if (m_playing) return;
        activeCut().removeCameraKey(m_currentFrame);
        markCutDirty(activeCut());
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::clearAllKeysRequested, this, [this] {
        if (m_playing) return;
        activeCut().clearCameraKeys();
        markCutDirty(activeCut());
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::showFrameToggled, this, [this](bool) { updateCameraOverlay(); });

    m_referencePanel = new ReferencePanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_referencePanel);
    connect(m_referencePanel, &ReferencePanel::boardSelected, this, [this](int index) {
        m_referenceBoardIndex = index;
        updateReferencePanel();
    });
    // 参照ドックの色指定行クリック: パレットパネルの色選択と同じ経路でペン色に反映する
    connect(m_referencePanel, &ReferencePanel::colorPicked, this, [this](QColor color) {
        m_penColor = color;
        m_canvas->setPenColor(m_penColor);
        updatePenColorButton();
    });

    updateLayerPanel();
    updatePalettePanel();
    updateXsheetPanel();  // 末尾でupdateCelPanel()も呼ばれる
    updateReferencePanel();
}

void MainWindow::updateLayerPanel() {
    if (!m_layerPanel) return;
    core::Cel& cel = activeCel();
    QStringList names;
    QList<bool> visible;
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        const core::Layer& layer = cel.layer(li);
        QString displayName = QString::fromStdString(layer.name());
        // 種別に応じて表示名にサフィックスを付ける(最終画には含めない種別が分かるように)
        switch (layer.role()) {
            case core::LayerRole::ColorTrace:
                displayName += tr(" [色トレス]");
                break;
            case core::LayerRole::Correction:
                displayName += tr(" [修正]");
                break;
            case core::LayerRole::Normal:
            default:
                break;
        }
        names.append(displayName);
        visible.append(layer.visible());
    }
    m_layerPanel->setLayers(names, visible, static_cast<int>(m_activeLayer));
    m_layerPanel->setWindowTitle(tr("レイヤー - セル %1").arg(QString::fromStdString(cel.name())));
}

void MainWindow::addLayerToActiveCel() {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    const size_t frameCount = activeLayer().frameCount();
    core::Layer& layer = cel.addLayer(tr("レイヤー %1").arg(cel.layerCount() + 1).toStdString());
    for (size_t fi = 0; fi < frameCount; ++fi) {
        layer.addFrame().bitmap() = makeTransparentCelForCel(cel, canvasWidth(), canvasHeight());  // 既存レイヤーとコマ数(・用紙サイズ)を揃える
    }
    m_activeLayer = cel.layerCount() - 1;
    m_commands.clear();
    m_canvas->clearTextureCache();
    markCutDirty(activeCut());
    updateCanvasLayers();
    updateOnionSkin();
    updateLayerPanel();
    updateWindowTitle();
}

void MainWindow::removeActiveLayer() {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    if (cel.layerCount() <= 1) return;  // 最後の1枚は消さない
    cel.removeLayer(m_activeLayer);
    m_activeLayer = std::min(m_activeLayer, cel.layerCount() - 1);
    m_commands.clear();
    m_canvas->clearTextureCache();
    markCutDirty(activeCut());
    updateCanvasLayers();
    updateOnionSkin();
    updateLayerPanel();
    updateWindowTitle();
}

void MainWindow::moveActiveLayer(int delta) {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    const int from = static_cast<int>(m_activeLayer);
    const int to = from + delta;
    if (to < 0 || static_cast<size_t>(to) >= cel.layerCount()) return;
    cel.moveLayer(static_cast<size_t>(from), static_cast<size_t>(to));
    m_activeLayer = static_cast<size_t>(to);
    m_commands.clear();
    m_canvas->clearTextureCache();
    markCutDirty(activeCut());
    updateCanvasLayers();
    updateOnionSkin();
    updateLayerPanel();
    updateWindowTitle();
}

void MainWindow::updatePalettePanel() {
    if (!m_palettePanel) return;
    QList<QColor> colors;
    for (const core::Bitmap::Pixel& color : m_project->palette()) {
        colors.append(QColor(color.r, color.g, color.b, color.a));
    }
    m_palettePanel->setPalette(colors);
}

void MainWindow::addCurrentColorToPalette() {
    m_project->palette().push_back({static_cast<uint8_t>(m_penColor.red()), static_cast<uint8_t>(m_penColor.green()),
                                     static_cast<uint8_t>(m_penColor.blue()), static_cast<uint8_t>(m_penColor.alpha())});
    markProjectDirty();  // パレットはproject.ppamに格納する
    updatePalettePanel();
    updateWindowTitle();
}

void MainWindow::removeSelectedPaletteColor() {
    if (!m_palettePanel) return;
    const int index = m_palettePanel->selectedIndex();
    auto& palette = m_project->palette();
    if (index < 0 || static_cast<size_t>(index) >= palette.size()) return;
    palette.erase(palette.begin() + index);
    markProjectDirty();
    updatePalettePanel();
    updateWindowTitle();
}

// activeCut()の全セルから露出表を集めてタイムシートパネルに反映する
void MainWindow::updateXsheetPanel() {
    if (!m_xsheetPanel) return;
    core::Cut& cut = activeCut();

    QStringList celNames;
    QList<bool> celVisible;
    QList<QList<int>> exposures;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        const core::Cel& cel = cut.cel(ci);
        celNames.append(QString::fromStdString(cel.name()));
        celVisible.append(cel.visible());
        QList<int> column;
        column.reserve(static_cast<int>(cut.frameCount()));
        for (size_t f = 0; f < cut.frameCount(); ++f) {
            column.append(cel.exposure(f));
        }
        exposures.append(column);
    }

    m_xsheetPanel->setSheet(celNames, celVisible, exposures, static_cast<int>(cut.frameCount()),
                             static_cast<int>(m_currentFrame), static_cast<int>(m_activeCel));

    updateCelPanel();  // セルの構成・可視状態・アクティブセルはXsheetと同じ元データなのでここで一緒に更新する
    updateTapPanel();  // 位置キーの一覧・現在コマの選択もアクティブセルに追従させる
    updateCameraPanel();  // カメラフレーム(カット単位)も現在コマ・現在カットに追従させる
}

// activeCut()の全セルからセル名・可視状態を集めてセルパネルに反映する
void MainWindow::updateCelPanel() {
    if (!m_celPanel) return;
    core::Cut& cut = activeCut();

    QStringList celNames;
    QList<bool> celVisible;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        const core::Cel& cel = cut.cel(ci);
        celNames.append(QString::fromStdString(cel.name()));
        celVisible.append(cel.visible());
    }

    m_celPanel->setCels(celNames, celVisible, static_cast<int>(m_activeCel));
}

// アクティブセルの位置キー一覧(コマ昇順)をタップパネルに反映する
void MainWindow::updateTapPanel() {
    if (!m_tapPanel) return;
    const core::Cel& cel = activeCel();

    QList<std::tuple<int, float, float>> keys;
    for (const auto& [frame, position] : cel.positionKeys()) {
        keys.append({static_cast<int>(frame), position.x, position.y});
    }

    m_tapPanel->setKeys(keys, static_cast<int>(m_currentFrame));
}

// アクティブカットのカメラフレーム(画面に写る範囲)を現在コマに合わせてカメラパネルへ反映する。
// キー規則: キーが無ければ基本状態(キャンバス中心・100%)を表示、あれば現在コマの値
// (補間含む)を表示する
void MainWindow::updateCameraPanel() {
    if (!m_cameraPanel) return;
    core::Cut& cut = activeCut();
    const auto camera = cut.cameraFrameAt(m_currentFrame);
    if (camera) {
        m_cameraPanel->setValues(camera->center.x, camera->center.y, camera->scale * 100.0);
    } else {
        m_cameraPanel->setValues(canvasWidth() / 2.0, canvasHeight() / 2.0, 100.0);
    }

    const bool hasKeyOnFrame = cut.cameraKeys().count(m_currentFrame) > 0;
    const bool hasAnyKeys = !cut.cameraKeys().empty();
    m_cameraPanel->setKeyState(hasKeyOnFrame, hasAnyKeys);

    updateCameraOverlay();
}

// カメラパネルの現在の表示値(スピン)からキャンバスのオーバーレイ矩形を作り直す。
// 「枠を表示」がOFFなら非表示にする。スピン編集中のプレビュー表示にも使う
void MainWindow::updateCameraOverlay() {
    if (!m_cameraPanel) return;
    if (!m_cameraPanel->showFrameEnabled()) {
        m_canvas->setCameraFrameOverlay(QRectF());
        return;
    }
    const double scale = std::max(0.05, m_cameraPanel->scalePercent() / 100.0);
    const double w = canvasWidth() * scale;
    const double h = canvasHeight() * scale;
    const double cx = m_cameraPanel->centerX();
    const double cy = m_cameraPanel->centerY();
    m_canvas->setCameraFrameOverlay(QRectF(cx - w / 2.0, cy - h / 2.0, w, h));
}

// 新しいセルを1枚追加する。名前は自動連番(1文字目のセルは新規文書時に"A"が既にあるので、
// 2枚目以降を"B","C",...と割り当て、26枚を超えたら"セル27"のような形式にする)
void MainWindow::addCel() {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    const int index = static_cast<int>(cut.celCount());
    const QString name = index < 26 ? QString(QChar('A' + index)) : tr("セル%1").arg(index + 1);

    core::Cel& cel = cut.addCel(name.toStdString());
    core::Layer& layer = cel.addLayer(tr("レイヤー 1").toStdString());
    layer.addFrame().bitmap() = makeTransparentCel(canvasWidth(), canvasHeight());
    cel.setExposure(m_currentFrame, 0);  // 現在コマに動画1を割り付け、すぐ描ける状態にする
    m_activeCel = static_cast<size_t>(index);

    // 新規セル・レイヤー・動画は既存のBitmapに影響しないためUndo履歴/テクスチャキャッシュの破棄は不要
    markCutDirty(cut);
    updateCanvasLayers();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

void MainWindow::removeActiveCel() {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    if (cut.celCount() <= 1) return;  // 最後の1枚は消さない

    cut.removeCel(m_activeCel);
    m_activeCel = std::min(m_activeCel, cut.celCount() - 1);

    m_commands.clear();             // 構造変更のためUndo履歴を破棄
    m_canvas->clearTextureCache();  // セルのBitmapが破棄されたため
    markCutDirty(cut);
    updateCanvasLayers();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

void MainWindow::renameActiveCel() {
    if (m_playing) return;
    core::Cel& cel = activeCel();

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("セル名を変更"), tr("セル名:"), QLineEdit::Normal,
                                                    QString::fromStdString(cel.name()), &ok);
    if (!ok || newName.isEmpty()) return;  // キャンセルまたは空欄は変更しない
    cel.setName(newName.toStdString());

    markCutDirty(activeCut());
    updateCanvasLayers();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

// 引きセル: アクティブセルの用紙サイズ変更ダイアログを開く。OKで確定した場合、
// セル内の全レイヤー・全フレームのビットマップを新サイズへ中央基準で移し替える
void MainWindow::openCelSizeDialog() {
    if (m_playing) return;
    core::Cel& cel = activeCel();

    CelSizeDialog dialog(cel.paperWidth(), cel.paperHeight(), canvasWidth(), canvasHeight(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int newW = dialog.paperWidth();
    const int newH = dialog.paperHeight();
    // 現在有効なサイズ(0ならキャンバスサイズ)と比較し、実質変更なしなら何もしない
    const int effectiveW = cel.paperWidth() > 0 ? cel.paperWidth() : canvasWidth();
    const int effectiveH = cel.paperHeight() > 0 ? cel.paperHeight() : canvasHeight();
    if (newW == effectiveW && newH == effectiveH) return;

    cel.resizePaper(newW, newH);

    m_commands.clear();             // 全ビットマップが再確保されたためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 同上、テクスチャキャッシュも破棄
    markCutDirty(activeCut());
    updateCanvasLayers();
    updateOnionSkin();
    updateWindowTitle();
}

// プロジェクトのキャンバス解像度・アスペクト比を変更するダイアログを開く(ファイルメニューから)。
// OKで確定した場合、これから描く紙のサイズ(新規セル・合成・書き出し)だけが変わる。
// 既存の作画セルのビットマップはリサイズしない(paperサイズを持つ場合はそのまま引きセル扱いになる)
void MainWindow::openCanvasSizeDialog() {
    if (!m_project) return;

    CanvasSizeDialog dialog(canvasWidth(), canvasHeight(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int newW = dialog.canvasWidth();
    const int newH = dialog.canvasHeight();
    if (newW == canvasWidth() && newH == canvasHeight()) return;

    debugSetCanvasSize(newW, newH);
    updateWindowTitle();
}

// 解像度設定確認用/内部共通処理: プロジェクトのキャンバスサイズを変更し、
// 作画キャンバス・撮影/編集ウィンドウ・プリビズ下敷きへ反映する(ダイアログは出さない)
void MainWindow::debugSetCanvasSize(int width, int height) {
    if (!m_project) return;
    m_project->setCanvasSize(width, height);

    m_canvas->setCanvasSize(canvasWidth(), canvasHeight());
    updateCanvasLayers();
    if (m_editWindow) m_editWindow->setCanvasSize(canvasWidth(), canvasHeight());
    if (m_shootingWindow) m_shootingWindow->setCanvasSize(canvasWidth(), canvasHeight());
    if (m_previzUnderlay) updateUnderlay();  // 下敷きのアスペクトを新キャンバスサイズへ合わせて再計算する
    markProjectDirty();
}

// 解像度設定確認用: 「プロジェクト設定...」ダイアログを非モーダルで開き、そのポインタを返す
// (呼び出し側でウィンドウ全体+ダイアログを合成してスクリーンショットするために使う)
QDialog* MainWindow::debugOpenCanvasSizeDialog() {
    auto* dialog = new CanvasSizeDialog(canvasWidth(), canvasHeight(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return dialog;
}

// アクティブセルの重なり順を移動する(delta=-1:下/奥へ、+1:上/手前へ)
void MainWindow::moveActiveCel(int delta) {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    const int from = static_cast<int>(m_activeCel);
    const int to = from + delta;
    if (to < 0 || static_cast<size_t>(to) >= cut.celCount()) return;

    cut.moveCel(static_cast<size_t>(from), static_cast<size_t>(to));
    m_activeCel = static_cast<size_t>(to);

    // 重なり順が変わるだけでBitmapは無傷なためテクスチャキャッシュの破棄は不要
    markCutDirty(cut);
    updateCanvasLayers();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

// アクティブセルから動画(絵)1枚を削除する。全レイヤーから該当コマを取り除き、
// 露出表(タイムシートの割付)を詰め直す
void MainWindow::deleteDrawing(int idx) {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    if (cel.drawingCount() <= 1) return;  // 最後の1枚は消さない
    if (idx < 0 || static_cast<size_t>(idx) >= cel.drawingCount()) return;

    for (size_t li = 0; li < cel.layerCount(); ++li) {
        cel.layer(li).removeFrame(static_cast<size_t>(idx));
    }

    // 露出表を修正: 削除された動画を指していたコマは空欄(-1)に、それより後ろの動画番号は1つ詰める
    core::Cut& cut = activeCut();
    for (size_t f = 0; f < cut.frameCount(); ++f) {
        const int e = cel.exposure(f);
        if (e == idx) {
            cel.setExposure(f, -1);
        } else if (e > idx) {
            cel.setExposure(f, e - 1);
        }
    }

    m_commands.clear();             // 動画のBitmapが破棄されたためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 同上、テクスチャキャッシュも破棄
    markCutDirty(cut);
    updateCanvasLayers();
    updateOnionSkin();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

void MainWindow::duplicateDrawing(int idx) {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    if (idx < 0 || static_cast<size_t>(idx) >= cel.drawingCount()) return;

    // 新しい動画を作る(セル内全レイヤーに同時追加し、複製元動画idxの内容をコピーする)。
    // 割付規則はaddFrameAfterCurrentと同じ: 現在コマが空欄ならその場、割付済みなら次コマ・尺延長
    const int newDrawing = static_cast<int>(cel.drawingCount());
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        core::Layer& layer = cel.layer(li);
        const core::Bitmap sourceBitmap = layer.frame(static_cast<size_t>(idx)).bitmap();  // addFrame前にコピーしておく
        layer.addFrame().bitmap() = sourceBitmap;
    }
    const bool currentIsEmpty = cel.exposure(m_currentFrame) == -1;
    const size_t target = currentIsEmpty ? m_currentFrame : m_currentFrame + 1;
    core::Cut& cut = activeCut();
    if (target >= cut.frameCount()) cut.setFrameCount(target + 1);
    cel.setExposure(target, newDrawing);

    m_commands.clear();             // 構造変更のためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 動画構造が変わったため
    setCurrentFrame(target);
    markCutDirty(cut);
    updateWindowTitle();
}

void MainWindow::setCelVisibility(int celIndex, bool visible) {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;

    cut.cel(static_cast<size_t>(celIndex)).setVisible(visible);

    markCutDirty(cut);
    updateCanvasLayers();
    updateXsheetPanel();  // 末尾でupdateCelPanel()も呼ばれる
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
}

// アクティブセルを切り替える(CelPanelでのセル選択で使用。Xsheetのセルクリックはコマ移動を伴うため
// setCurrentFrame()経由で別途処理している)
void MainWindow::setActiveCel(int celIndex) {
    core::Cut& cut = activeCut();
    if (cut.celCount() == 0 || celIndex < 0) return;
    m_activeCel = static_cast<size_t>(std::min(celIndex, static_cast<int>(cut.celCount()) - 1));

    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateLayerPanel();
    updateXsheetPanel();  // 末尾でupdateCelPanel()も呼ばれる
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("ファイル(&F)"));

    QAction* newAction = fileMenu->addAction(tr("新規(&N)"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);

    QAction* openAction = fileMenu->addAction(tr("開く(&O)..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::open);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction(tr("保存(&S)"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::save);

    QAction* saveAsAction = fileMenu->addAction(tr("名前を付けて保存(&A)..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    fileMenu->addSeparator();

    QAction* exportAction = fileMenu->addAction(tr("書き出し(&E)..."));
    exportAction->setShortcut(QKeySequence(tr("Ctrl+E")));
    connect(exportAction, &QAction::triggered, this, &MainWindow::openExportDialog);

    fileMenu->addSeparator();

    QAction* canvasSizeAction = fileMenu->addAction(tr("プロジェクト設定(&R)..."));
    connect(canvasSizeAction, &QAction::triggered, this, &MainWindow::openCanvasSizeDialog);

    // 編集メニュー
    QMenu* editMenu = menuBar()->addMenu(tr("編集(&E)"));
    QAction* undoAction = editMenu->addAction(tr("元に戻す(&U)"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &MainWindow::undo);
    QAction* redoAction = editMenu->addAction(tr("やり直す(&R)"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::redo);

    // プリビズメニュー(別ウィンドウ)
    QMenu* previzMenu = menuBar()->addMenu(tr("プリビズ(&P)"));
    QAction* previzAction = previzMenu->addAction(tr("プリビズウィンドウ(&W)"));
    previzAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(previzAction, &QAction::triggered, this, &MainWindow::openPrevizWindow);
    QAction* previzUnderlayAction = previzMenu->addAction(tr("プリビズを下敷きにする(&U)"));
    previzUnderlayAction->setCheckable(true);
    previzUnderlayAction->setShortcut(QKeySequence(Qt::Key_F6));
    connect(previzUnderlayAction, &QAction::toggled, this, [this](bool checked) { setPrevizUnderlay(checked); });

    // 絵コンテメニュー(別ウィンドウ)
    QMenu* storyboardMenu = menuBar()->addMenu(tr("絵コンテ(&S)"));
    QAction* storyboardAction = storyboardMenu->addAction(tr("絵コンテウィンドウ(&W)"));
    storyboardAction->setShortcut(QKeySequence(Qt::Key_F7));
    connect(storyboardAction, &QAction::triggered, this, &MainWindow::openStoryboardWindow);

    // 設定ボードメニュー(別ウィンドウ)
    QMenu* settingBoardMenu = menuBar()->addMenu(tr("設定ボード(&B)"));
    QAction* settingBoardAction = settingBoardMenu->addAction(tr("設定ボードウィンドウ(&W)"));
    settingBoardAction->setShortcut(QKeySequence(Qt::Key_F8));
    connect(settingBoardAction, &QAction::triggered, this, &MainWindow::openSettingBoardWindow);

    // 編集(カッティング)メニュー(別ウィンドウ。カット並べ替え・尺調整・進捗管理・通しプレビュー)
    QMenu* editWindowMenu = menuBar()->addMenu(tr("編集(カッティング)(&D)"));
    QAction* editWindowAction = editWindowMenu->addAction(tr("編集ウィンドウ(&W)"));
    editWindowAction->setShortcut(QKeySequence(Qt::Key_F9));
    connect(editWindowAction, &QAction::triggered, this, &MainWindow::openEditWindow);

    // 撮影メニュー(別ウィンドウ。エフェクトスタック+撮影シート(行=エフェクト、列=コマ))
    QMenu* shootingMenu = menuBar()->addMenu(tr("撮影(&T)"));
    QAction* shootingAction = shootingMenu->addAction(tr("撮影ウィンドウ(&W)"));
    shootingAction->setShortcut(QKeySequence(Qt::Key_F10));
    connect(shootingAction, &QAction::triggered, this, &MainWindow::openShootingWindow);

    // 表示メニュー: 各ドックパネルの表示/非表示(パネル追加時はここに並べる)
    QMenu* viewMenu = menuBar()->addMenu(tr("表示(&V)"));
    viewMenu->addAction(m_framePanel->toggleViewAction());
    viewMenu->addAction(m_layerPanel->toggleViewAction());
    viewMenu->addAction(m_palettePanel->toggleViewAction());
    viewMenu->addAction(m_xsheetPanel->toggleViewAction());
    viewMenu->addAction(m_celPanel->toggleViewAction());
    viewMenu->addAction(m_tapPanel->toggleViewAction());
    viewMenu->addAction(m_referencePanel->toggleViewAction());
    viewMenu->addSeparator();
    // 仕上げ表示: 色トレス線・作監修正レイヤーを隠して最終画を確認する(書き出しと同じ見え方)
    QAction* cleanViewAction = viewMenu->addAction(tr("仕上げ表示(トレス線/修正を隠す)(&C)"));
    cleanViewAction->setCheckable(true);
    cleanViewAction->setShortcut(QKeySequence(Qt::Key_T));
    connect(cleanViewAction, &QAction::toggled, this, [this](bool checked) {
        m_cleanView = checked;
        updateCanvasLayers();
    });
    // 左右反転表示(ミラーチェック): デッサンの狂いを確認する定番機能
    QAction* mirrorAction = viewMenu->addAction(tr("左右反転表示(&M)"));
    mirrorAction->setCheckable(true);
    mirrorAction->setShortcut(QKeySequence(Qt::Key_H));
    connect(mirrorAction, &QAction::toggled, this, [this](bool checked) { m_canvas->setMirrorView(checked); });

    QAction* resetViewAction = viewMenu->addAction(tr("ビューをリセット(&R)"));
    resetViewAction->setShortcut(QKeySequence(tr("Ctrl+0")));
    connect(resetViewAction, &QAction::triggered, this, [this] { m_canvas->resetView(); });

    // レイアウト用フレーム枠ガイド: 作画フレーム(100%)/TVセーフ(約90%)/タイトルセーフ(約80%)を
    // キャンバスに重ね表示する。状態はセッション限定でppamには保存しない
    QAction* frameGuideAction = viewMenu->addAction(tr("フレーム枠(&G)"));
    frameGuideAction->setCheckable(true);
    frameGuideAction->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(frameGuideAction, &QAction::toggled, this, [this](bool checked) { m_canvas->setFrameGuides(checked); });

    // 下敷きメニュー: 3DCGレンダリングや写真をキャンバスに薄く透かして表示する参照機能。
    // セッション限定であり、.ppamプロジェクトファイルには保存されない
    QMenu* underlayMenu = menuBar()->addMenu(tr("下敷き(&U)"));

    QAction* openUnderlayAction = underlayMenu->addAction(tr("画像/連番を開く(&O)..."));
    connect(openUnderlayAction, &QAction::triggered, this, &MainWindow::openUnderlay);

    QAction* clearUnderlayAction = underlayMenu->addAction(tr("クリア(&C)"));
    connect(clearUnderlayAction, &QAction::triggered, this, &MainWindow::clearUnderlaySequence);

    QMenu* underlayOpacityMenu = underlayMenu->addMenu(tr("不透明度"));
    auto* opacityGroup = new QActionGroup(this);
    opacityGroup->setExclusive(true);
    const int opacityPercents[] = {25, 50, 75};
    for (int percent : opacityPercents) {
        QAction* opacityAction = underlayOpacityMenu->addAction(tr("%1%").arg(percent));
        opacityAction->setCheckable(true);
        opacityAction->setChecked(percent == 50);  // 既定50%
        opacityGroup->addAction(opacityAction);
        connect(opacityAction, &QAction::triggered, this,
                [this, percent] { m_canvas->setUnderlayOpacity(static_cast<float>(percent) / 100.0f); });
    }
}

void MainWindow::undo() {
    if (m_playing || !m_commands.canUndo()) return;
    core::Command* command = m_commands.undo();
    // 変更矩形が分かるコマンドは部分転送のみで済ませる(全テクスチャ破棄はカクつきの原因)
    if (auto* stroke = dynamic_cast<core::StrokeCommand*>(command)) {
        m_canvas->notifyBitmapRegionChanged(stroke->bitmap(), stroke->region());
    } else if (command) {
        m_canvas->clearTextureCache();
    }
    if (command) {
        // コマンドがどのカットのBitmapを指しているか特定できないため安全側(全カット)で扱う
        markAllDirty();
        updateWindowTitle();
    }
}

void MainWindow::redo() {
    if (m_playing || !m_commands.canRedo()) return;
    core::Command* command = m_commands.redo();
    if (auto* stroke = dynamic_cast<core::StrokeCommand*>(command)) {
        m_canvas->notifyBitmapRegionChanged(stroke->bitmap(), stroke->region());
    } else if (command) {
        m_canvas->clearTextureCache();
    }
    if (command) {
        markAllDirty();
        updateWindowTitle();
    }
}

void MainWindow::updateWindowTitle() {
    const QString base = QStringLiteral("perapera-anime-maker901");
    QString title = base;
    if (!m_currentFilePath.isEmpty()) {
        // m_currentFilePathは常に.ppprojフォルダのパス。タイトルには拡張子を除いたフォルダ名を出す
        QString folderName = QFileInfo(m_currentFilePath).fileName();
        if (folderName.endsWith(QStringLiteral(".ppproj"), Qt::CaseInsensitive)) {
            folderName.chop(7);  // ".ppproj" の7文字を除く
        }
        title = QStringLiteral("%1 - %2").arg(base, folderName);
    }
    if (m_dirty) {
        title = QStringLiteral("*%1").arg(title);  // 未保存の変更があることを示す
    }
    setWindowTitle(title);
}

void MainWindow::newDocument() {
    if (m_playing) togglePlayback();
    m_commands.clear();  // 旧プロジェクトのBitmapを参照するコマンドを破棄
    createNewDocument();
    m_canvas->clearTextureCache();  // 旧プロジェクトのBitmapポインタ再利用に備えて破棄
    if (m_previzWindow) m_previzWindow->setScene(&activeCut().previz());  // 旧シーンへのポインタを差し替え
    if (m_storyboardWindow) {
        m_storyboardWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_storyboardWindow->refresh();
    }
    if (m_settingBoardWindow) {
        m_settingBoardWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_settingBoardWindow->refresh();
    }
    if (m_editWindow) {
        m_editWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_editWindow->refresh();
    }
    if (m_shootingWindow) {
        m_shootingWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_shootingWindow->refresh();
    }
    m_referenceBoardIndex = -1;
    updateReferencePanel();
    updateFrameLabel();
    updateLayerPanel();
    updatePalettePanel();
    updateXsheetPanel();
    m_currentFilePath.clear();
    m_dirty = false;
    m_dirtyScope.clear();
    updateWindowTitle();
}

bool MainWindow::saveToFile(const QString& path, const core::SaveOptions* options) {
    std::string error;
    if (!core::ProjectIO::save(*m_project, std::filesystem::path(path.toStdWString()), &error, options)) {
        QMessageBox::warning(this, tr("保存エラー"), QString::fromStdString(error));
        return false;
    }
    m_currentFilePath = path;
    m_dirty = false;
    m_dirtyScope.clear();
    updateWindowTitle();
    return true;
}

bool MainWindow::loadFromFile(const QString& path) {
    std::string error;
    auto project = core::ProjectIO::load(std::filesystem::path(path.toStdWString()), &error);
    if (!project) {
        QMessageBox::warning(this, tr("読み込みエラー"), QString::fromStdString(error));
        return false;
    }
    // MVPで扱える構造(1シーン・1カット・1レイヤー・1フレーム以上)を検証する
    if (project->sceneCount() == 0 || project->scene(0).cutCount() == 0 || project->scene(0).cut(0).celCount() == 0 ||
        project->scene(0).cut(0).cel(0).layerCount() == 0 ||
        project->scene(0).cut(0).cel(0).layer(0).frameCount() == 0) {
        QMessageBox::warning(this, tr("読み込みエラー"), tr("プロジェクトにフレームがありません"));
        return false;
    }

    if (m_playing) togglePlayback();
    m_commands.clear();  // 旧プロジェクトのBitmapを参照するコマンドを破棄
    m_project = std::move(project);
    m_activeCut = 0;
    m_activeCel = 0;
    m_activeLayer = 0;
    // 読み込んだプロジェクトのキャンバスサイズへ作画キャンバス・撮影/編集ウィンドウを合わせる
    m_canvas->setCanvasSize(canvasWidth(), canvasHeight());
    if (m_editWindow) m_editWindow->setCanvasSize(canvasWidth(), canvasHeight());
    if (m_shootingWindow) m_shootingWindow->setCanvasSize(canvasWidth(), canvasHeight());
    m_canvas->clearTextureCache();
    if (m_previzWindow) m_previzWindow->setScene(&activeCut().previz());  // 旧シーンへのポインタを差し替え
    if (m_storyboardWindow) {
        m_storyboardWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_storyboardWindow->refresh();
    }
    if (m_settingBoardWindow) {
        m_settingBoardWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_settingBoardWindow->refresh();
    }
    if (m_editWindow) {
        m_editWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_editWindow->refresh();
    }
    if (m_shootingWindow) {
        m_shootingWindow->setProject(m_project.get());  // プロジェクト差し替え
        m_shootingWindow->refresh();
    }
    m_referenceBoardIndex = -1;
    updateReferencePanel();
    setCurrentFrame(0);
    updateLayerPanel();
    updateCutBar();
    updatePalettePanel();
    updateXsheetPanel();
    // m_currentFilePathは常に.ppprojフォルダのパスにする。project.ppamファイル自体を指すパスで
    // 開いた場合(open()のダイアログ経由)は親フォルダへ正規化する
    const QFileInfo pathInfo(path);
    m_currentFilePath = pathInfo.fileName().compare(QStringLiteral("project.ppam"), Qt::CaseInsensitive) == 0
                             ? pathInfo.path()
                             : path;
    m_dirty = false;
    m_dirtyScope.clear();
    updateWindowTitle();
    return true;
}

void MainWindow::save() {
    if (m_currentFilePath.isEmpty()) {
        saveAs();
        return;
    }
    // 上書き保存: 変更範囲(DirtyScope)だけを書き直す。新規保存先(project.ppam不在)なら
    // ProjectIO::save側でoptionsを無視して全書きするので、ここでの判定は不要
    core::SaveOptions options;
    options.writeProject = m_dirtyScope.project;
    options.writeStoryboard = m_dirtyScope.storyboard;
    options.writeBoards = m_dirtyScope.boards;
    options.writeAllCuts = m_dirtyScope.allCuts;
    options.cutIds = m_dirtyScope.cutIds;
    saveToFile(m_currentFilePath, &options);
}

void MainWindow::saveAs() {
    QString path =
        QFileDialog::getSaveFileName(this, tr("プロジェクトを保存"), QString(), tr("perapera プロジェクト (*.ppproj)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".ppproj"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".ppproj");
    }
    saveToFile(path);  // 名前を付けて保存は常に全ファイル書き出し(options=nullptr)
}

void MainWindow::open() {
    // .ppprojフォルダの中のproject.ppamを選ばせる(loadFromFileはフォルダ/project.ppamどちらも受け付ける)
    const QString path = QFileDialog::getOpenFileName(this, tr("プロジェクトを開く"), QString(),
                                                        tr("perapera プロジェクト (project.ppam);;すべて (*.*)"));
    if (!path.isEmpty()) loadFromFile(path);
}

void MainWindow::setupToolBar() {
    QToolBar* toolBar = addToolBar(tr("Tools"));
    toolBar->setMovable(false);

    // --- 描画ツール ---
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    QAction* penAction = toolBar->addAction(tr("ペン"));
    penAction->setCheckable(true);
    penAction->setChecked(true);
    penAction->setShortcut(QKeySequence(Qt::Key_P));
    group->addAction(penAction);
    connect(penAction, &QAction::triggered, this, [this] {
        m_canvas->setTool(GLCanvas::Tool::Pen);
        m_penRadiusSlider->setValue(m_penRadiusValue);  // ペンの記憶値をスライダーに反映
    });

    QAction* eraserAction = toolBar->addAction(tr("消しゴム"));
    eraserAction->setCheckable(true);
    eraserAction->setShortcut(QKeySequence(Qt::Key_E));
    group->addAction(eraserAction);
    connect(eraserAction, &QAction::triggered, this, [this] {
        m_canvas->setTool(GLCanvas::Tool::Eraser);
        m_penRadiusSlider->setValue(m_eraserRadiusValue);  // 消しゴムの記憶値をスライダーに反映
    });

    QAction* fillAction = toolBar->addAction(tr("塗りつぶし"));
    fillAction->setCheckable(true);
    fillAction->setShortcut(QKeySequence(Qt::Key_F));
    group->addAction(fillAction);
    connect(fillAction, &QAction::triggered, this, [this] { m_canvas->setTool(GLCanvas::Tool::Fill); });

    QAction* moveAction = toolBar->addAction(tr("移動"));
    moveAction->setCheckable(true);
    moveAction->setShortcut(QKeySequence(Qt::Key_V));
    group->addAction(moveAction);
    connect(moveAction, &QAction::triggered, this, [this] { m_canvas->setTool(GLCanvas::Tool::Move); });

    toolBar->addSeparator();

    // --- ブラシ設定(太さ・色) ---
    toolBar->addWidget(new QLabel(tr(" 太さ: "), this));

    m_penRadiusSlider = new QSlider(Qt::Horizontal, this);
    m_penRadiusSlider->setRange(1, 64);
    m_penRadiusSlider->setValue(6);
    m_penRadiusSlider->setFixedWidth(120);
    // Spaceキーでの再生操作にフォーカスを奪わないよう、クリック時のみフォーカスを持たせる
    m_penRadiusSlider->setFocusPolicy(Qt::ClickFocus);
    connect(m_penRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        // 現在ツールに応じて太さを反映する。塗りつぶしツールはペン扱いのままでよい
        if (m_canvas->tool() == GLCanvas::Tool::Eraser) {
            m_canvas->setEraserRadius(static_cast<float>(value));
            m_eraserRadiusValue = value;
        } else {
            m_canvas->setPenRadius(static_cast<float>(value));
            m_penRadiusValue = value;
        }
        m_penRadiusValueLabel->setText(QString::number(value));
    });
    toolBar->addWidget(m_penRadiusSlider);

    m_penRadiusValueLabel = new QLabel(QString::number(m_penRadiusSlider->value()), this);
    toolBar->addWidget(m_penRadiusValueLabel);

    m_penColorButton = new QToolButton(this);
    m_penColorButton->setFixedSize(24, 24);
    m_penColorButton->setToolTip(tr("ペンの色"));
    connect(m_penColorButton, &QToolButton::clicked, this, &MainWindow::choosePenColor);
    toolBar->addWidget(m_penColorButton);
    updatePenColorButton();

    // 手ブレ補正(0=なし〜100=最大)
    toolBar->addWidget(new QLabel(tr(" 補正: "), this));
    auto* stabilizerSpin = new QSpinBox(this);
    stabilizerSpin->setRange(0, 100);
    stabilizerSpin->setValue(20);
    stabilizerSpin->setFocusPolicy(Qt::ClickFocus);
    stabilizerSpin->setToolTip(tr("手ブレ補正の強さ"));
    connect(stabilizerSpin, &QSpinBox::valueChanged, this, [this](int v) { m_canvas->setStabilizer(v); });
    toolBar->addWidget(stabilizerSpin);

    // 筆圧検知のon/off。offにするとペン圧を無視して常に最大筆圧(線幅一定)で描く。
    // 筆圧非対応ペンや、意図せず筆圧で細くなるのを避けたいときに使う
    auto* pressureCheck = new QCheckBox(tr("筆圧"), this);
    pressureCheck->setChecked(m_canvas->pressureEnabled());
    pressureCheck->setFocusPolicy(Qt::ClickFocus);
    pressureCheck->setToolTip(tr("筆圧検知(offで線幅一定)"));
    connect(pressureCheck, &QCheckBox::toggled, this, [this](bool checked) { m_canvas->setPressureEnabled(checked); });
    toolBar->addWidget(pressureCheck);

    toolBar->addSeparator();

    // --- フレーム操作 ---
    QAction* prevAction = toolBar->addAction(tr("前のコマ"));
    prevAction->setShortcut(QKeySequence(Qt::Key_Comma));
    connect(prevAction, &QAction::triggered, this, [this] {
        if (!m_playing && m_currentFrame > 0) setCurrentFrame(m_currentFrame - 1);
    });

    QAction* nextAction = toolBar->addAction(tr("次のコマ"));
    nextAction->setShortcut(QKeySequence(Qt::Key_Period));
    connect(nextAction, &QAction::triggered, this, [this] {
        if (!m_playing) setCurrentFrame(m_currentFrame + 1);
    });

    QAction* addAction = toolBar->addAction(tr("動画追加"));
    // Key_Aは「操作」メニューのゲーム風ショートカット(前のコマ)に割り当てるため、ここでは単キーを持たせない
    connect(addAction, &QAction::triggered, this, &MainWindow::addFrameAfterCurrent);

    QAction* deleteAction = toolBar->addAction(tr("割付クリア"));
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteAction, &QAction::triggered, this, &MainWindow::deleteCurrentFrame);

    m_frameLabel = new QLabel(this);
    toolBar->addWidget(m_frameLabel);

    toolBar->addSeparator();

    // --- オニオンスキン ---
    m_onionAction = toolBar->addAction(tr("オニオンスキン"));
    m_onionAction->setCheckable(true);
    m_onionAction->setChecked(m_onionEnabled);
    m_onionAction->setShortcut(QKeySequence(Qt::Key_O));
    connect(m_onionAction, &QAction::toggled, this, [this](bool checked) {
        m_onionEnabled = checked;
        updateOnionSkin();
    });

    toolBar->addSeparator();

    // --- 再生 ---
    m_playAction = toolBar->addAction(tr("再生 (Space)"));
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);

    toolBar->addWidget(new QLabel(tr(" FPS: "), this));
    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(1, 60);
    m_fpsSpin->setValue(kDefaultFps);
    m_fpsSpin->setFocusPolicy(Qt::ClickFocus);
    connect(m_fpsSpin, &QSpinBox::valueChanged, this, [this](int fps) {
        if (m_playing) m_playTimer->start(1000 / std::max(1, fps));
    });
    toolBar->addWidget(m_fpsSpin);

    // --- 操作メニュー(ゲーム風ショートカット) ---
    // ユーザー要望: 「コマ送りにいちいちタイムシートをクリックするのが面倒。よく使う機能をWASDなどゲーム感覚で」
    // よく使う操作を単キーに割り当てる。一覧として見えるようメニュー化する。
    // 既にツールバー/メニューで使われているキー(P/E/F/V/Space等)と衝突する場合は、
    // 新規ショートカットを持たせず既存アクションをそのままtrigger()する(表示は一覧化のためテキストにキーを明記)
    QMenu* operationMenu = menuBar()->addMenu(tr("操作(&K)"));

    QAction* prevFrameKeyAction = operationMenu->addAction(tr("前のコマ (A)"));
    prevFrameKeyAction->setShortcut(QKeySequence(Qt::Key_A));
    connect(prevFrameKeyAction, &QAction::triggered, prevAction, &QAction::trigger);

    QAction* nextFrameKeyAction = operationMenu->addAction(tr("次のコマ (D)"));
    nextFrameKeyAction->setShortcut(QKeySequence(Qt::Key_D));
    connect(nextFrameKeyAction, &QAction::triggered, nextAction, &QAction::trigger);

    operationMenu->addSeparator();

    QAction* prevCelKeyAction = operationMenu->addAction(tr("上のセル (W)"));
    prevCelKeyAction->setShortcut(QKeySequence(Qt::Key_W));
    connect(prevCelKeyAction, &QAction::triggered, this, [this] {
        if (m_playing) return;
        setActiveCel(static_cast<int>(m_activeCel) - 1);
    });

    QAction* nextCelKeyAction = operationMenu->addAction(tr("下のセル (S)"));
    nextCelKeyAction->setShortcut(QKeySequence(Qt::Key_S));
    connect(nextCelKeyAction, &QAction::triggered, this, [this] {
        if (m_playing) return;
        setActiveCel(static_cast<int>(m_activeCel) + 1);
    });

    operationMenu->addSeparator();

    QAction* playKeyAction = operationMenu->addAction(tr("再生/停止 (Space)"));
    // Spaceは既にm_playActionが持っているため、新規ショートカットは持たせず同じアクションをtriggerする
    connect(playKeyAction, &QAction::triggered, m_playAction, &QAction::trigger);

    operationMenu->addSeparator();

    QAction* step1KeyAction = operationMenu->addAction(tr("1コマ打ち (1)"));
    step1KeyAction->setShortcut(QKeySequence(Qt::Key_1));
    connect(step1KeyAction, &QAction::triggered, this, [this] { applyStepPattern(1); });

    QAction* step2KeyAction = operationMenu->addAction(tr("2コマ打ち (2)"));
    step2KeyAction->setShortcut(QKeySequence(Qt::Key_2));
    connect(step2KeyAction, &QAction::triggered, this, [this] { applyStepPattern(2); });

    QAction* step3KeyAction = operationMenu->addAction(tr("3コマ打ち (3)"));
    step3KeyAction->setShortcut(QKeySequence(Qt::Key_3));
    connect(step3KeyAction, &QAction::triggered, this, [this] { applyStepPattern(3); });

    operationMenu->addSeparator();

    QAction* penKeyAction = operationMenu->addAction(tr("ペン (B)"));
    penKeyAction->setShortcut(QKeySequence(Qt::Key_B));
    connect(penKeyAction, &QAction::triggered, penAction, &QAction::trigger);

    QAction* eraserKeyAction = operationMenu->addAction(tr("消しゴム (E)"));
    // Eは既にeraserActionが持っているため、新規ショートカットは持たせず同じアクションをtriggerする
    connect(eraserKeyAction, &QAction::triggered, eraserAction, &QAction::trigger);

    QAction* fillKeyAction = operationMenu->addAction(tr("塗りつぶし (G)"));
    fillKeyAction->setShortcut(QKeySequence(Qt::Key_G));
    connect(fillKeyAction, &QAction::triggered, fillAction, &QAction::trigger);

    QAction* moveKeyAction = operationMenu->addAction(tr("移動 (V)"));
    // Vは既にmoveActionが持っているため、新規ショートカットは持たせず同じアクションをtriggerする
    connect(moveKeyAction, &QAction::triggered, moveAction, &QAction::trigger);
}

void MainWindow::debugSetupOnionDemo() {
    // 3フレームにそれぞれ位置の異なる縦線を描き、中央フレームを表示する
    core::Layer& layer = activeLayer();
    while (layer.frameCount() < 3) {
        core::Frame& frame = layer.addFrame();
        frame.bitmap() = makeTransparentCel(canvasWidth(), canvasHeight());
    }

    core::BrushEngine engine;
    engine.settings().radius = 14.0f;
    engine.settings().color = {0, 0, 0, 255};
    for (int i = 0; i < 3; ++i) {
        core::Bitmap& bitmap = layer.frame(static_cast<size_t>(i)).bitmap();
        const float x = canvasWidth() * (0.30f + 0.20f * static_cast<float>(i));
        engine.beginStroke(bitmap, x, canvasHeight() * 0.25f, 0.9f);
        engine.continueStroke(bitmap, x, canvasHeight() * 0.75f, 0.9f);
        engine.endStroke();
    }

    // タイムシート: 尺3コマ、1コマ打ちで動画1,2,3を割付
    activeCut().setFrameCount(3);
    for (size_t t = 0; t < 3; ++t) activeCel().setExposure(t, static_cast<int>(t));

    m_canvas->clearTextureCache();
    m_onionEnabled = true;
    if (m_onionAction) m_onionAction->setChecked(true);
    setCurrentFrame(1);
}

void MainWindow::debugSetupLayerDemo() {
    // レイヤー2枚構成: 下=赤の縦線 / 上=青の横線
    addLayerToActiveCel();  // レイヤー2を追加(アクティブになる)
    core::Cel& cel = activeCel();

    core::BrushEngine engine;
    engine.settings().radius = 12.0f;

    core::Bitmap& bottom = cel.layer(0).frame(m_currentFrame).bitmap();
    engine.settings().color = {200, 40, 40, 255};
    engine.beginStroke(bottom, canvasWidth() * 0.4f, canvasHeight() * 0.25f, 0.9f);
    engine.continueStroke(bottom, canvasWidth() * 0.4f, canvasHeight() * 0.75f, 0.9f);
    engine.endStroke();

    core::Bitmap& top = cel.layer(1).frame(m_currentFrame).bitmap();
    engine.settings().color = {40, 40, 200, 255};
    engine.beginStroke(top, canvasWidth() * 0.25f, canvasHeight() * 0.5f, 0.9f);
    engine.continueStroke(top, canvasWidth() * 0.75f, canvasHeight() * 0.5f, 0.9f);
    engine.endStroke();

    m_canvas->clearTextureCache();
    updateCanvasLayers();
    updateLayerPanel();
}

void MainWindow::debugSetupTapDemo() {
    // 動画1(左上寄りの矩形枠)を尺3で止めにし、コマ1→3で右下へ移動するキーを打つ
    debugSetupFillDemo();  // 矩形枠を描く
    core::Cut& cut = activeCut();
    core::Cel& cel = activeCel();
    cut.setFrameCount(3);
    for (size_t t = 0; t < 3; ++t) cel.setExposure(t, 0);  // 止め
    cel.setPositionKey(0, {0.0f, 0.0f});
    cel.setPositionKey(2, {400.0f, 200.0f});
    setCurrentFrame(0);
}

void MainWindow::debugSetupOversizeDemo() {
    // 引きセル: アクティブセルをキャンバス幅の2倍(横パン用の背景セル)にリサイズする。
    // 既存の(キャンバスサイズの)動画1は中央基準で新サイズへ移し替えられる(元々空なので実害なし)
    core::Cel& cel = activeCel();
    cel.resizePaper(canvasWidth() * 2, canvasHeight());
    m_canvas->clearTextureCache();

    // 用紙全域(縦方向は上〜下いっぱい)に、左半分=赤・右半分=青の縦線を描く
    core::Bitmap& bitmap = activeLayer().frame(0).bitmap();
    core::BrushEngine engine;
    engine.settings().radius = 20.0f;

    engine.settings().color = {220, 30, 30, 255};  // 左半分: 赤
    const float leftX = canvasWidth() * 0.5f;
    engine.beginStroke(bitmap, leftX, canvasHeight() * 0.1f, 1.0f);
    engine.continueStroke(bitmap, leftX, canvasHeight() * 0.9f, 1.0f);
    engine.endStroke();

    engine.settings().color = {30, 60, 220, 255};  // 右半分: 青
    const float rightX = canvasWidth() * 1.5f;
    engine.beginStroke(bitmap, rightX, canvasHeight() * 0.1f, 1.0f);
    engine.continueStroke(bitmap, rightX, canvasHeight() * 0.9f, 1.0f);
    engine.endStroke();

    // 位置キー: コマ0=オフセット0(左半分の赤がキャンバス内)、
    // コマ2=左へキャンバス幅ぶんパン(セルが左へずれ、右半分の青がキャンバス内に来る)
    core::Cut& cut = activeCut();
    cut.setFrameCount(3);
    for (size_t t = 0; t < 3; ++t) cel.setExposure(t, 0);  // 止め(同じ動画をコマ0〜2に表示)
    cel.setPositionKey(0, {0.0f, 0.0f});
    cel.setPositionKey(2, {static_cast<float>(-canvasWidth()), 0.0f});

    markCutDirty(cut);
    updateWindowTitle();
    setCurrentFrame(0);
}

void MainWindow::debugSetupCameraDemo() {
    // ストローク1本を描き、カット尺を24コマにしてカメラキーを2つ打つ
    // (コマ0=中心・100%、コマ23=左上寄り・50%)、コマ12(中間点)へ移動する
    debugSetupFillDemo();  // 目印になる矩形枠を描く
    core::Cut& cut = activeCut();
    core::Cel& cel = activeCel();
    cut.setFrameCount(24);
    for (size_t t = 0; t < 24; ++t) cel.setExposure(t, 0);  // 止め(全コマで同じ絵を表示)
    cut.setCameraKey(0, core::CameraFrameState{{canvasWidth() / 2.0f, canvasHeight() / 2.0f}, 1.0});
    cut.setCameraKey(23, core::CameraFrameState{{canvasWidth() * 0.3f, canvasHeight() * 0.3f}, 0.5});
    markCutDirty(cut);
    updateWindowTitle();
    setCurrentFrame(12);
}

void MainWindow::debugSetupShootingDemo() {
    // 撮影ウィンドウ確認用: ストローク1本(矩形枠)を描いて尺24の止めにし、
    // 全体ブラー(コマ0=半径0→コマ23=半径10のキー、画面右半分だけに効くマスク付き)+全体パラ
    // (キー無し)を組んで撮影ウィンドウを開き、シートのコマ12を選択する
    debugSetupFillDemo();  // 目印になる矩形枠をアクティブセル(セル0)に描く

    core::Cut& cut = activeCut();
    core::Cel& cel = activeCel();
    cut.setFrameCount(24);
    for (size_t t = 0; t < 24; ++t) cel.setExposure(t, 0);  // 止め(全コマで同じ絵を表示)
    cut.effects().clear();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.enabled = true;
    blur.targetCel = -1;  // 全体
    blur.params = core::effectDefaultParams(core::EffectType::Blur);
    blur.setKey("radius", 0, 0.0);    // 冒頭はボケなし
    blur.setKey("radius", 23, 10.0);  // 末尾へ向けてボケていく(フォーカスアウト)

    // マスク編集(「特定の部分にエフェクトをかけたい」要望)確認用: 画面の右半分だけを赤(alpha255)で
    // 塗ったマスクを設定する。プレビューでは右半分だけボケる見え方になる
    core::Bitmap blurMask(canvasWidth(), canvasHeight());
    blurMask.fill({0, 0, 0, 0});
    for (int y = 0; y < canvasHeight(); ++y) {
        for (int x = canvasWidth() / 2; x < canvasWidth(); ++x) blurMask.setPixel(x, y, {255, 0, 0, 255});
    }
    blur.mask = std::move(blurMask);

    cut.effects().push_back(blur);

    core::Effect para;
    para.type = core::EffectType::Para;
    para.enabled = true;
    para.targetCel = -1;  // 全体
    para.params = core::effectDefaultParams(core::EffectType::Para);
    cut.effects().push_back(para);

    markCutDirty(cut);
    updateWindowTitle();
    updateXsheetPanel();

    openShootingWindow();
    m_shootingWindow->debugSelectKoma(12);
}

void MainWindow::debugSetupClassicDemo() {
    // クラシック撮影(マルチプレーン撮影台)確認用: セルA=赤い矩形枠(遠景、距離500mm/幅400mm)、
    // セルB=左寄り緑丸(近景、距離300mm/幅300mm)を作り、f/2.0・フォーカス500mm・samples=8で
    // マルチプレーン撮影を有効化して撮影ウィンドウを開く(Bはピントが合わずボケる想定)
    core::Cut& cut = activeCut();

    // セルA: 赤い矩形枠(既定のアクティブセルに描く)
    core::Bitmap& bitmapA = activeLayer().frame(m_currentFrame).bitmap();
    core::BrushEngine engineA;
    engineA.settings().radius = 10.0f;
    engineA.settings().color = {210, 30, 30, 255};
    const float ax0 = canvasWidth() * 0.30f, ax1 = canvasWidth() * 0.70f;
    const float ay0 = canvasHeight() * 0.30f, ay1 = canvasHeight() * 0.70f;
    engineA.beginStroke(bitmapA, ax0, ay0, 1.0f);
    engineA.continueStroke(bitmapA, ax1, ay0, 1.0f);
    engineA.continueStroke(bitmapA, ax1, ay1, 1.0f);
    engineA.continueStroke(bitmapA, ax0, ay1, 1.0f);
    engineA.continueStroke(bitmapA, ax0, ay0, 1.0f);
    engineA.endStroke();

    // セルB: 左寄り緑丸(1スタンプの円ダブ)
    addCel();  // セルB追加(アクティブになる。現在コマに動画1が自動で割り付く)
    core::Cel& celB = cut.cel(1);
    core::Bitmap& bitmapB = celB.layer(0).frame(0).bitmap();
    core::BrushEngine engineB;
    engineB.settings().radius = 80.0f;
    engineB.settings().color = {30, 180, 60, 255};
    engineB.beginStroke(bitmapB, canvasWidth() * 0.25f, canvasHeight() * 0.5f, 1.0f);
    engineB.endStroke();

    // クラシック撮影(マルチプレーン)を有効化
    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.focalLengthMm = 50.0;
    mp.camera.sensorWidthMm = 36.0;
    mp.camera.apertureFStop = 2.0;
    mp.camera.focusDistanceMm = 500.0;
    mp.samplesPerPixel = 8;
    mp.planes.clear();
    core::MultiplaneCelPlane planeA;
    planeA.celIndex = 0;
    planeA.distanceMm = 500.0;
    planeA.widthMm = 400.0;
    mp.planes.push_back(planeA);
    core::MultiplaneCelPlane planeB;
    planeB.celIndex = 1;
    planeB.distanceMm = 300.0;
    planeB.widthMm = 300.0;
    mp.planes.push_back(planeB);

    m_canvas->clearTextureCache();
    updateCanvasLayers();
    markCutDirty(cut);
    updateWindowTitle();

    openShootingWindow();
}

void MainWindow::debugSetupBacklightDemo() {
    // 透過光(T光)確認用: アクティブセルを黒で全面塗りし、中央付近に丸い穴(消しゴムで
    // アルファを0に戻す=未塗り)をいくつか開ける。その平面をクラシック撮影に距離500mm/幅400mmで
    // 割り付け、f/2.0・フォーカス250mm(=500mmの穴平面はピント外れ=玉ボケになる)にし、
    // 透過光を2灯構成で有効化して撮影ウィンドウを開く(複数灯UIの確認用):
    // 灯1「メイン」=点滅キー(偶数コマ消灯/奇数コマ点灯4、白色)、
    // 灯2「色付き」=常時2.0のシアン、ペンマスクで画面左半分だけ光らせる
    core::Cut& cut = activeCut();
    core::Bitmap& bitmap = activeLayer().frame(m_currentFrame).bitmap();
    bitmap.fill({0, 0, 0, 255});  // 黒で全面塗り(未塗り部だけが透過光を通す)

    core::BrushEngine holeEngine;
    holeEngine.settings().radius = 36.0f;
    holeEngine.settings().mode = core::BrushMode::Erase;  // 消しゴムで穴を開ける(alphaを0に戻す)
    const struct {
        float dx, dy;
    } kHoles[] = {
        {0.0f, 0.0f}, {90.0f, -60.0f}, {-90.0f, -60.0f}, {70.0f, 80.0f}, {-70.0f, 80.0f},
    };
    for (const auto& hole : kHoles) {
        const float hx = canvasWidth() * 0.5f + hole.dx;
        const float hy = canvasHeight() * 0.5f + hole.dy;
        holeEngine.beginStroke(bitmap, hx, hy, 1.0f);
        holeEngine.endStroke();
    }

    // クラシック撮影(マルチプレーン)を有効化
    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.focalLengthMm = 50.0;
    mp.camera.sensorWidthMm = 36.0;
    mp.camera.apertureFStop = 2.0;
    mp.camera.focusDistanceMm = 250.0;  // 穴の平面(500mm)より手前にピント=穴はピンボケ(玉ボケ)
    mp.samplesPerPixel = 16;
    mp.planes.clear();
    core::MultiplaneCelPlane plane;
    plane.celIndex = 0;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    mp.planes.push_back(plane);

    // 透過光(T光)を2灯構成で有効化(複数灯UIの確認用)
    // 灯1「メイン」: 白色、蛍光灯/液晶の点滅(押井守風)を強度のコマキーで消灯↔点灯させる
    core::MultiplaneBacklight mainLight;
    mainLight.name = "メイン";
    mainLight.enabled = true;
    mainLight.intensity = 4.0;
    mainLight.colorR = 1.0;
    mainLight.colorG = 1.0;
    mainLight.colorB = 1.0;
    mainLight.bloomRadiusPx = 24.0;
    mainLight.bloomStrength = 0.8;
    for (size_t t = 0; t < 8; ++t) mainLight.intensityKeys[t] = (t % 2 == 0) ? 0.0 : 4.0;  // 偶数コマ=消灯

    // 灯2「色付き」: シアン、常時2.0で点灯したまま。ペンマスクで画面左半分だけ光らせる
    core::MultiplaneBacklight coloredLight;
    coloredLight.name = "色付き";
    coloredLight.enabled = true;
    coloredLight.intensity = 2.0;
    coloredLight.colorR = 0.2;
    coloredLight.colorG = 0.8;
    coloredLight.colorB = 1.0;
    {
        core::Bitmap mask(canvasWidth(), canvasHeight());
        mask.fill({0, 0, 0, 0});  // 全面透明(未塗り部=遮光)
        for (int y = 0; y < canvasHeight(); ++y) {
            for (int x = 0; x < canvasWidth() / 2; ++x) {
                mask.setPixel(x, y, {255, 0, 0, 255});  // 左半分だけ塗って光を通す
            }
        }
        coloredLight.mask = std::move(mask);
    }

    mp.backlights.clear();
    mp.backlights.push_back(std::move(mainLight));
    mp.backlights.push_back(std::move(coloredLight));

    cut.setFrameCount(8);
    core::Cel& cel = activeCel();
    for (size_t t = 0; t < 8; ++t) cel.setExposure(t, 0);  // 止め
    // 滑らかなカメラ変化: 焦点距離50→70mm、フォーカス250→500mmへキー補間
    mp.focalKeys.clear();
    mp.focalKeys[0] = 50.0;
    mp.focalKeys[7] = 70.0;
    mp.focusKeys.clear();
    mp.focusKeys[0] = 250.0;
    mp.focusKeys[7] = 500.0;

    m_canvas->clearTextureCache();
    updateCanvasLayers();
    updateXsheetPanel();
    markCutDirty(cut);
    updateWindowTitle();

    openShootingWindow();
}

void MainWindow::debugSetupFilmDemo() {
    // フィルムエフェクト確認用: グレー地にカラーバー風の縦帯数本を描いた止めセルを尺4で組み、
    // 全体にフィルムエフェクト(既定値、粒状を目視しやすくするためgrain=0.5)をかけて撮影ウィンドウを開く
    core::Cut& cut = activeCut();
    core::Bitmap& bitmap = activeLayer().frame(m_currentFrame).bitmap();
    bitmap.fill({128, 128, 128, 255});  // グレー地

    const core::Bitmap::Pixel kBars[] = {
        {230, 40, 40, 255}, {230, 200, 40, 255}, {40, 200, 60, 255}, {40, 140, 230, 255}, {200, 40, 200, 255},
    };
    const int barCount = static_cast<int>(sizeof(kBars) / sizeof(kBars[0]));
    const int barWidth = canvasWidth() / (barCount + 2);  // 左右に余白(グレー地)を残す
    for (int i = 0; i < barCount; ++i) {
        const int x0 = barWidth + i * barWidth;
        const int x1 = std::min(canvasWidth(), x0 + barWidth);
        for (int y = canvasHeight() / 4; y < canvasHeight() * 3 / 4; ++y) {
            for (int x = x0; x < x1; ++x) bitmap.setPixel(x, y, kBars[i]);
        }
    }

    cut.setFrameCount(4);
    core::Cel& cel = activeCel();
    for (size_t t = 0; t < 4; ++t) cel.setExposure(t, 0);  // 止め(全コマで同じ絵を表示)
    cut.effects().clear();

    core::Effect film;
    film.type = core::EffectType::Film;
    film.enabled = true;
    film.targetCel = -1;  // 全体
    film.params = core::effectDefaultParams(core::EffectType::Film);
    film.params["grain"] = 0.5;  // 粒状を目視しやすいよう強めにする
    cut.effects().push_back(film);

    m_canvas->clearTextureCache();
    updateCanvasLayers();
    markCutDirty(cut);
    updateWindowTitle();
    updateXsheetPanel();

    openShootingWindow();
}

void MainWindow::debugBuildFullDemo() {
    // これまで実装した全機能を1本の作品として統合したデモを、newDocument()相当から
    // プログラム的に構築する(作画は手描きできないためBrushEngineで幾何図形を描く)。
    debugNewDocument();
    core::Scene& scene = m_project->scene(0);

    // --- 絵コンテ(データとして。動画には出ない) ---
    auto& panels = scene.storyboard();
    panels.clear();
    {
        constexpr int kSheetWidth = 1920;
        constexpr int kSheetHeight = 600;
        core::BrushEngine sbEngine;
        sbEngine.settings().radius = 8.0f;
        sbEngine.settings().color = {60, 60, 60, 255};

        core::StoryboardPanel panel1;
        panel1.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
        panel1.drawing.fill({0, 0, 0, 0});
        panel1.cutLabel = "1";
        panel1.action = "背景が左へパンしながらキャラが歩く。カメラがゆっくり寄る(T.U.)。";
        panel1.dialogue = "行くぞ!";
        panel1.durationFrames = 48;
        sbEngine.beginStroke(panel1.drawing, 200.0f, 100.0f, 1.0f);
        sbEngine.continueStroke(panel1.drawing, 800.0f, 500.0f, 1.0f);
        sbEngine.endStroke();
        panels.push_back(std::move(panel1));

        core::StoryboardPanel panel2;
        panel2.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
        panel2.drawing.fill({0, 0, 0, 0});
        panel2.cutLabel = "2";
        panel2.action = "クラシック撮影(マルチプレーン)。手前のキャラにピント、奥の背景がボケる。";
        panel2.dialogue = "";
        panel2.durationFrames = 36;
        sbEngine.beginStroke(panel2.drawing, 300.0f, 150.0f, 1.0f);
        sbEngine.continueStroke(panel2.drawing, 900.0f, 450.0f, 1.0f);
        sbEngine.endStroke();
        panels.push_back(std::move(panel2));
    }

    // --- 設定ボード(データとして) ---
    auto& boards = m_project->settingBoards();
    boards.clear();
    {
        core::SettingBoard board;
        board.name = "キャラ設定";
        board.image = core::Bitmap(canvasWidth(), canvasHeight());
        board.image.fill({0, 0, 0, 0});
        core::BrushEngine boardEngine;
        boardEngine.settings().radius = 8.0f;
        boardEngine.settings().color = {90, 70, 60, 255};
        boardEngine.beginStroke(board.image, canvasWidth() * 0.3f, canvasHeight() * 0.3f, 1.0f);
        boardEngine.continueStroke(board.image, canvasWidth() * 0.7f, canvasHeight() * 0.7f, 1.0f);
        boardEngine.endStroke();
        board.colorSpecs.push_back({"肌", {255, 224, 196, 255}});
        board.colorSpecs.push_back({"影", {200, 170, 150, 255}});
        boards.push_back(std::move(board));
    }

    // ==================================================================
    // カット0「プリビズなぞり作画」(尺24コマ)
    // ==================================================================
    // newDocument()で作られた最初のカット(セルA=空)を先頭カットとして使う。
    // 注意: renderCameraViewImage()はFBOレンダのため、プリビズウィンドウが表示されて
    // GLコンテキスト初期化(initializeGL)が済んでいる必要がある。--fulldemoフック側で
    // 事前にプリビズを開いて数百ms待ってから本関数を呼ぶ2段構えを前提にしている
    // (未オープンならここで開くが、直後にGL未初期化のまま描画すると落ちる可能性があるため保険に留める)
    core::Cut& cut0 = scene.cut(0);
    cut0.setName("カット0 プリビズなぞり作画");
    cut0.setAction("プリビズ(3DCG)を下敷きに、筆圧ペンで箱の輪郭をなぞって作画する。");
    cut0.setDialogue("");
    cut0.setStatus(core::CutStatus::Layout);  // Lo なぞり作画
    cut0.setFrameCount(24);

    if (!m_previzWindow) openPrevizWindow();
    m_previzWindow->setScene(&cut0.previz());  // 空シーンには箱モデルが自動追加される(PrevizWindow::setScene)
    m_previzWindow->setTimeline(0, cut0.frameCount());
    const QImage previzImage =
        m_previzWindow->viewport()->renderCameraViewImage(static_cast<float>(canvasWidth()) / canvasHeight());

    // 背景セル(プリビズ下敷き): カメラ画像をキャンバスサイズへアスペクト維持でスケールし、
    // アルファを下げて淡く焼き込む。全コマ止め
    core::Cel& previzBgCel = cut0.cel(0);
    previzBgCel.setName("プリビズ下敷き");
    previzBgCel.layer(0).frame(0).bitmap() = makeFaintPrevizBitmap(previzImage, canvasWidth(), canvasHeight(), 0.35f);
    for (size_t t = 0; t < cut0.frameCount(); ++t) previzBgCel.setExposure(t, 0);

    // キャラセル(筆圧ペンでなぞり線): 箱の投影位置は厳密でなくてよいので、画面中央付近に
    // 矩形+対角線を「筆圧ペンでなぞった」線で描く(両端が細く中央が太い)
    core::Cel& tracingCel = cut0.addCel("なぞり線");
    core::Layer& tracingLayer = tracingCel.addLayer("レイヤー 1");
    core::Bitmap& tracingBitmap = tracingLayer.addFrame().bitmap();
    tracingBitmap = makeTransparentCel(canvasWidth(), canvasHeight());
    {
        const float x0 = canvasWidth() * 0.32f, x1 = canvasWidth() * 0.68f;
        const float y0 = canvasHeight() * 0.30f, y1 = canvasHeight() * 0.72f;
        const QColor penColor(30, 30, 40);
        // 箱の輪郭をなぞった風の矩形
        drawPenStroke(tracingBitmap, {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}}, penColor, 14.0f);
        // 奥行きを感じさせる対角線のなぞり線
        drawPenStroke(tracingBitmap, {{x0, y0}, {x1, y1}}, penColor, 10.0f);
        drawPenStroke(tracingBitmap, {{x1, y0}, {x0, y1}}, penColor, 10.0f);
    }
    for (size_t t = 0; t < cut0.frameCount(); ++t) tracingCel.setExposure(t, 0);

    // ==================================================================
    // カット1「PAN + T.U. + グロー」(尺48コマ)
    // ==================================================================
    core::Cut& cut1 = scene.addCut("カット1 PAN+TU+グロー");
    initializeCut(cut1, canvasWidth(), canvasHeight());
    cut1.setAction("背景が左へパンしながらキャラが歩く。カメラがゆっくり寄る(T.U.)。");
    cut1.setDialogue("行くぞ!");
    cut1.setStatus(core::CutStatus::Done);
    cut1.setFrameCount(48);

    // 背景セル(引きセル): 横2倍にリサイズしてから地平線+縦グリッド線を描く
    core::Cel& bgCel = cut1.cel(0);
    bgCel.setName("背景");
    bgCel.resizePaper(canvasWidth() * 2, canvasHeight());
    {
        core::Bitmap& bgBitmap = bgCel.layer(0).frame(0).bitmap();
        core::BrushEngine bgEngine;
        bgEngine.settings().radius = 6.0f;
        bgEngine.settings().color = {70, 70, 85, 255};
        // 地平線
        bgEngine.beginStroke(bgBitmap, 0.0f, canvasHeight() * 0.6f, 1.0f);
        bgEngine.continueStroke(bgBitmap, static_cast<float>(canvasWidth() * 2), canvasHeight() * 0.6f, 1.0f);
        bgEngine.endStroke();
        // 縦グリッド線(地面の遠近感)
        for (int i = 0; i <= 16; ++i) {
            const float x = static_cast<float>(canvasWidth() * 2) * (static_cast<float>(i) / 16.0f);
            bgEngine.beginStroke(bgBitmap, x, canvasHeight() * 0.6f, 1.0f);
            bgEngine.continueStroke(bgBitmap, x, static_cast<float>(canvasHeight()), 1.0f);
            bgEngine.endStroke();
        }
    }
    for (size_t t = 0; t < 48; ++t) bgCel.setExposure(t, 0);  // 止め(1枚絵をPANで動かす)
    bgCel.setPositionKey(0, {0.0f, 0.0f});
    bgCel.setPositionKey(47, {-static_cast<float>(canvasWidth()), 0.0f});  // 左へPAN

    // キャラセル(通常サイズ): 3枚の動画で歩きのパラパラ(2コマ打ち、明るいハイライト付き=グロー確認用)
    core::Cel& charCel = cut1.addCel("キャラ");
    core::Layer& charLayer = charCel.addLayer("レイヤー 1");
    constexpr int kCharDrawings = 3;
    for (int i = 0; i < kCharDrawings; ++i) {
        core::Bitmap& bmp = charLayer.addFrame().bitmap();
        bmp = makeTransparentCel(canvasWidth(), canvasHeight());
        core::BrushEngine charEngine;
        const float cx = canvasWidth() * 0.5f;
        const float cy = canvasHeight() * 0.55f + (i == 1 ? -20.0f : 0.0f);  // 中割りだけ跳ねる

        // 頭(円)
        charEngine.settings().radius = 60.0f;
        charEngine.settings().color = {230, 90, 60, 255};
        charEngine.beginStroke(bmp, cx, cy - 150.0f, 1.0f);
        charEngine.endStroke();
        // 胴体
        charEngine.settings().radius = 20.0f;
        charEngine.beginStroke(bmp, cx, cy - 90.0f, 1.0f);
        charEngine.continueStroke(bmp, cx, cy + 60.0f, 1.0f);
        charEngine.endStroke();
        // 脚(コマごとに開閉させて歩行に見せる)
        const float legOffset = 40.0f + static_cast<float>(i) * 15.0f;
        charEngine.settings().radius = 16.0f;
        charEngine.beginStroke(bmp, cx, cy + 60.0f, 1.0f);
        charEngine.continueStroke(bmp, cx - legOffset, cy + 160.0f, 1.0f);
        charEngine.endStroke();
        charEngine.beginStroke(bmp, cx, cy + 60.0f, 1.0f);
        charEngine.continueStroke(bmp, cx + legOffset, cy + 160.0f, 1.0f);
        charEngine.endStroke();
        // グロー確認用ハイライト(輝度200以上の明るい点)
        charEngine.settings().radius = 10.0f;
        charEngine.settings().color = {255, 255, 210, 255};
        charEngine.beginStroke(bmp, cx + 15.0f, cy - 165.0f, 1.0f);
        charEngine.endStroke();
    }
    for (size_t t = 0; t < 48; ++t) {
        charCel.setExposure(t, static_cast<int>((t / 2) % static_cast<size_t>(kCharDrawings)));  // 2コマ打ちで循環
    }

    // カメラフレームキー: コマ0=中心100%、コマ47=やや上へ寄って60%(T.U.=画面が寄っていく)
    cut1.setCameraKey(0, core::CameraFrameState{{canvasWidth() / 2.0f, canvasHeight() / 2.0f}, 1.0});
    cut1.setCameraKey(47, core::CameraFrameState{{canvasWidth() * 0.5f, canvasHeight() * 0.4f}, 0.6});

    // エフェクト: グロー(だんだん発光)
    {
        core::Effect glow;
        glow.type = core::EffectType::Glow;
        glow.enabled = true;
        glow.targetCel = -1;
        glow.params = core::effectDefaultParams(core::EffectType::Glow);
        glow.setKey("strength", 0, 0.0);
        glow.setKey("strength", 47, 0.9);
        cut1.effects().push_back(glow);
    }

    // ==================================================================
    // カット2「クラシック撮影(マルチプレーン)DoF」(尺36コマ)
    // ==================================================================
    core::Cut& cut2 = scene.addCut("カット2 クラシック撮影DoF");
    initializeCut(cut2, canvasWidth(), canvasHeight());
    cut2.setAction("マルチプレーン撮影(クラシック)。手前のキャラにピントを合わせ、奥の背景をぼかす。");
    cut2.setDialogue("");
    cut2.setStatus(core::CutStatus::Shooting);
    cut2.setFrameCount(36);

    // セルA(手前・キャラ)
    core::Cel& celA2 = cut2.cel(0);
    celA2.setName("キャラ");
    {
        core::Bitmap& bitmapA2 = celA2.layer(0).frame(0).bitmap();
        core::BrushEngine engineA2;
        engineA2.settings().radius = 120.0f;
        engineA2.settings().color = {230, 90, 60, 255};
        engineA2.beginStroke(bitmapA2, canvasWidth() * 0.5f, canvasHeight() * 0.5f, 1.0f);
        engineA2.endStroke();
    }
    for (size_t t = 0; t < 36; ++t) celA2.setExposure(t, 0);

    // セルB(奥・背景): グリッド模様
    core::Cel& celB2 = cut2.addCel("背景");
    core::Layer& layerB2 = celB2.addLayer("レイヤー 1");
    {
        core::Bitmap& bitmapB2 = layerB2.addFrame().bitmap();
        bitmapB2 = makeTransparentCel(canvasWidth(), canvasHeight());
        core::BrushEngine engineB2;
        engineB2.settings().radius = 8.0f;
        engineB2.settings().color = {120, 150, 190, 255};
        for (int i = 0; i <= 8; ++i) {
            const float x = canvasWidth() * (static_cast<float>(i) / 8.0f);
            engineB2.beginStroke(bitmapB2, x, 0.0f, 1.0f);
            engineB2.continueStroke(bitmapB2, x, static_cast<float>(canvasHeight()), 1.0f);
            engineB2.endStroke();
        }
        for (int j = 0; j <= 4; ++j) {
            const float y = canvasHeight() * (static_cast<float>(j) / 4.0f);
            engineB2.beginStroke(bitmapB2, 0.0f, y, 1.0f);
            engineB2.continueStroke(bitmapB2, static_cast<float>(canvasWidth()), y, 1.0f);
            engineB2.endStroke();
        }
    }
    for (size_t t = 0; t < 36; ++t) celB2.setExposure(t, 0);

    // マルチプレーン(クラシック撮影)を有効化: 手前A(300mm)にピント→奥B(1000mm)がボケる
    {
        core::MultiplaneSetup& mp = cut2.multiplane();
        mp.enabled = true;
        mp.camera.focalLengthMm = 50.0;
        mp.camera.sensorWidthMm = 36.0;
        mp.camera.apertureFStop = 2.8;
        mp.camera.focusDistanceMm = 300.0;
        mp.samplesPerPixel = 8;
        mp.planes.clear();
        core::MultiplaneCelPlane planeA;
        planeA.celIndex = 0;
        planeA.distanceMm = 300.0;
        planeA.widthMm = 300.0;
        mp.planes.push_back(planeA);
        core::MultiplaneCelPlane planeB;
        planeB.celIndex = 1;
        planeB.distanceMm = 1000.0;
        planeB.widthMm = 800.0;
        mp.planes.push_back(planeB);
    }

    // エフェクト: 黒パラ(上を暗く)
    {
        core::Effect para2;
        para2.type = core::EffectType::Para;
        para2.enabled = true;
        para2.targetCel = -1;
        para2.params = core::effectDefaultParams(core::EffectType::Para);
        para2.params["top"] = 0.3;
        para2.params["r"] = 0.0;
        para2.params["g"] = 0.0;
        para2.params["b"] = 0.0;
        cut2.effects().push_back(para2);
    }

    // ==================================================================
    // カット3「シェイク + カラーパラ」(尺24コマ)
    // ==================================================================
    core::Cut& cut3 = scene.addCut("カット3 シェイク+カラーパラ");
    initializeCut(cut3, canvasWidth(), canvasHeight());
    cut3.setAction("爆発の衝撃でカメラが激しく揺れ、徐々に収まる。オレンジのパラで画面を焼く。");
    cut3.setDialogue("うわあっ!");
    cut3.setStatus(core::CutStatus::Finishing);
    cut3.setFrameCount(24);

    // 爆発風の放射状ストローク
    core::Cel& cel3 = cut3.cel(0);
    cel3.setName("爆発");
    {
        core::Bitmap& bitmap3 = cel3.layer(0).frame(0).bitmap();
        core::BrushEngine engine3;
        engine3.settings().radius = 14.0f;
        engine3.settings().color = {255, 200, 40, 255};
        constexpr double kTau = 6.283185307179586;
        constexpr int kRays = 12;
        const float cx3 = canvasWidth() * 0.5f;
        const float cy3 = canvasHeight() * 0.5f;
        for (int i = 0; i < kRays; ++i) {
            const double angle = kTau * static_cast<double>(i) / static_cast<double>(kRays);
            const float ex = cx3 + static_cast<float>(std::cos(angle)) * canvasWidth() * 0.4f;
            const float ey = cy3 + static_cast<float>(std::sin(angle)) * canvasWidth() * 0.4f;
            engine3.beginStroke(bitmap3, cx3, cy3, 1.0f);
            engine3.continueStroke(bitmap3, ex, ey, 1.0f);
            engine3.endStroke();
        }
    }
    for (size_t t = 0; t < 24; ++t) cel3.setExposure(t, 0);

    // エフェクト: シェイク(序盤大きく→末尾0に収束)
    {
        core::Effect shake;
        shake.type = core::EffectType::Shake;
        shake.enabled = true;
        shake.targetCel = -1;
        shake.params = core::effectDefaultParams(core::EffectType::Shake);
        shake.setKey("amplitudeX", 0, 40.0);
        shake.setKey("amplitudeX", 23, 0.0);
        shake.setKey("amplitudeY", 0, 40.0);
        shake.setKey("amplitudeY", 23, 0.0);
        cut3.effects().push_back(shake);
    }

    // エフェクト: オレンジ寄りパラ
    {
        core::Effect para3;
        para3.type = core::EffectType::Para;
        para3.enabled = true;
        para3.targetCel = -1;
        para3.params = core::effectDefaultParams(core::EffectType::Para);
        para3.params["top"] = 0.4;
        para3.params["r"] = 255.0;
        para3.params["g"] = 120.0;
        para3.params["b"] = 0.0;
        cut3.effects().push_back(para3);
    }

    // 構築完了: アクティブカットをカット0(プリビズなぞり作画)に揃えて各パネルの整合を取る
    setActiveCut(0);
}

void MainWindow::debugSetupFillDemo() {
    // 閉じた矩形枠(黒)を現在フレームのアクティブレイヤーに描く
    core::Bitmap& bitmap = activeLayer().frame(m_currentFrame).bitmap();
    core::BrushEngine engine;
    engine.settings().radius = 8.0f;
    engine.settings().color = {0, 0, 0, 255};

    const float x0 = canvasWidth() * 0.30f, x1 = canvasWidth() * 0.70f;
    const float y0 = canvasHeight() * 0.30f, y1 = canvasHeight() * 0.70f;
    engine.beginStroke(bitmap, x0, y0, 1.0f);
    engine.continueStroke(bitmap, x1, y0, 1.0f);
    engine.continueStroke(bitmap, x1, y1, 1.0f);
    engine.continueStroke(bitmap, x0, y1, 1.0f);
    engine.continueStroke(bitmap, x0, y0, 1.0f);
    engine.endStroke();

    m_canvas->clearTextureCache();
    updateCanvasLayers();
}

void MainWindow::debugSetupColorTraceDemo() {
    // 1. 主線レイヤー(レイヤー1)に閉じた矩形枠を描く
    debugSetupFillDemo();

    // 2. 色トレス線レイヤーを追加し、矩形を左右に分ける赤い縦線を描く
    addLayerToActiveCel();  // レイヤー2(アクティブになる)
    activeLayer().setRole(core::LayerRole::ColorTrace);
    core::Bitmap& trace = activeLayer().frame(0).bitmap();
    core::BrushEngine engine;
    engine.settings().radius = 6.0f;
    engine.settings().color = {220, 30, 30, 255};  // 色トレス線(赤)
    engine.beginStroke(trace, canvasWidth() * 0.5f, canvasHeight() * 0.25f, 1.0f);
    engine.continueStroke(trace, canvasWidth() * 0.5f, canvasHeight() * 0.75f, 1.0f);
    engine.endStroke();

    // 3. 彩色用レイヤーを追加してアクティブに(塗り先)
    addLayerToActiveCel();  // レイヤー3
    m_canvas->clearTextureCache();
    updateCanvasLayers();
    updateLayerPanel();
}

void MainWindow::debugSetCleanView(bool enabled) {
    m_cleanView = enabled;
    updateCanvasLayers();
}

void MainWindow::debugSetLayerVisible(int layerIndex, bool visible) {
    core::Cel& cel = activeCel();
    if (static_cast<size_t>(layerIndex) >= cel.layerCount()) return;
    cel.layer(static_cast<size_t>(layerIndex)).setVisible(visible);
    updateCanvasLayers();
    updateLayerPanel();
}

void MainWindow::debugSetupXsheetDemo() {
    // オニオンデモで動画3枚(縦線位置の異なる絵)を用意した上で、
    // 尺6コマ・2コマ打ち(動画1,1,2,2,3,3)のタイムシートを組む
    debugSetupOnionDemo();
    activeCut().setFrameCount(6);
    activeCel().applyStepPattern(2, 6);

    // 露出(割付)だけの変更なのでテクスチャキャッシュの破棄は不要
    setCurrentFrame(0);  // コマ1(動画1)を表示
}

void MainWindow::debugSetupCelDemo() {
    // セル管理確認用: セルBを追加し、セルAに赤縦線・セルBに青横線を描く(共にコマ1に動画1を割付)
    addCel();  // セルB追加(動画1+exposure(0)=0はaddCel()内で設定済み。アクティブになる)
    core::Cut& cut = activeCut();

    core::BrushEngine engine;
    engine.settings().radius = 12.0f;

    core::Cel& celA = cut.cel(0);
    core::Bitmap& bitmapA = celA.layer(0).frame(0).bitmap();
    engine.settings().color = {200, 40, 40, 255};
    engine.beginStroke(bitmapA, canvasWidth() * 0.4f, canvasHeight() * 0.25f, 0.9f);
    engine.continueStroke(bitmapA, canvasWidth() * 0.4f, canvasHeight() * 0.75f, 0.9f);
    engine.endStroke();

    core::Cel& celB = cut.cel(1);
    core::Bitmap& bitmapB = celB.layer(0).frame(0).bitmap();
    engine.settings().color = {40, 40, 200, 255};
    engine.beginStroke(bitmapB, canvasWidth() * 0.25f, canvasHeight() * 0.5f, 0.9f);
    engine.continueStroke(bitmapB, canvasWidth() * 0.75f, canvasHeight() * 0.5f, 0.9f);
    engine.endStroke();

    celA.setExposure(0, 0);
    celB.setExposure(0, 0);

    m_canvas->clearTextureCache();
    setCurrentFrame(0);
}

void MainWindow::debugSetCelVisible(int celIndex, bool visible) {
    core::Cut& cut = activeCut();
    if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;
    cut.cel(static_cast<size_t>(celIndex)).setVisible(visible);
    updateCanvasLayers();
    updateXsheetPanel();
}

QString MainWindow::autosavePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(kAutosaveFileName);
}

bool MainWindow::performAutosave() {
    // ユーザーの保存(m_currentFilePath/m_dirty)とは無関係に、専用パスへ直接保存する
    const QString path = autosavePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    std::string error;
    if (!core::ProjectIO::save(*m_project, std::filesystem::path(path.toStdWString()), &error)) {
        return false;
    }
    statusBar()->showMessage(tr("自動保存しました"), 3000);
    return true;
}

QString MainWindow::debugTriggerAutosave() {
    return performAutosave() ? autosavePath() : QString();
}

int MainWindow::debugPaletteRoundTrip(const QString& projectPath) {
    // パレットに3色を追加→保存→新規(パレット空になる)→読込を行い、往復結果を検証する
    const core::Bitmap::Pixel expected[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    m_project->palette().clear();
    for (const core::Bitmap::Pixel& color : expected) {
        m_project->palette().push_back(color);
    }

    if (!saveToFile(projectPath)) return 1;
    newDocument();  // パレットが空に戻ることを確認する前提の状態にする
    if (!loadFromFile(projectPath)) return 1;

    const auto& loaded = m_project->palette();
    if (loaded.size() != 3) return 1;
    for (int i = 0; i < 3; ++i) {
        if (loaded[static_cast<size_t>(i)].r != expected[i].r || loaded[static_cast<size_t>(i)].g != expected[i].g ||
            loaded[static_cast<size_t>(i)].b != expected[i].b || loaded[static_cast<size_t>(i)].a != expected[i].a) {
            return 1;
        }
    }
    return 0;
}

int MainWindow::debugRoleRoundTrip(const QString& projectPath) {
    // レイヤーを2枚追加(計3枚)し、layer(1)をColorTrace、layer(2)をCorrectionに設定して
    // 保存→新規→読込を行い、種別が保持されているか検証する
    addLayerToActiveCel();  // レイヤー2枚目
    addLayerToActiveCel();  // レイヤー3枚目

    core::Cel& cel = activeCel();
    if (cel.layerCount() != 3) return 1;
    cel.layer(1).setRole(core::LayerRole::ColorTrace);
    cel.layer(2).setRole(core::LayerRole::Correction);

    if (!saveToFile(projectPath)) return 1;
    newDocument();  // 白紙に戻す
    if (!loadFromFile(projectPath)) return 1;

    core::Cel& loadedCel = activeCel();
    if (loadedCel.layerCount() != 3) return 1;
    if (loadedCel.layer(0).role() != core::LayerRole::Normal) return 1;
    if (loadedCel.layer(1).role() != core::LayerRole::ColorTrace) return 1;
    if (loadedCel.layer(2).role() != core::LayerRole::Correction) return 1;
    return 0;
}

void MainWindow::setPrevizUnderlay(bool enabled) {
    m_previzUnderlay = enabled;
    if (enabled) {
        if (!m_previzWindow) openPrevizWindow();
        updateUnderlay();
    } else {
        m_canvas->clearUnderlay();
        m_underlayLoadedIndex = -1;  // 連番下敷きへ戻る場合に再ロードさせる
        updateUnderlay();
    }
}

void MainWindow::openPrevizWindow() {
    if (!m_previzWindow) {
        m_previzWindow = new PrevizWindow(this);  // QMainWindowなので独立ウィンドウになる
        connect(m_previzWindow, &PrevizWindow::sceneEdited, this, [this] {
            // プリビズシーンはカット内(cut.previz())に格納される
            markCutDirty(activeCut());
            updateWindowTitle();
            if (m_previzUnderlay) updateUnderlay();  // カメラ/配置の変更を下敷きへ即反映
        });
        connect(m_previzWindow, &PrevizWindow::frameChangeRequested, this,
                [this](int frame) { setCurrentFrame(static_cast<size_t>(frame)); });  // シートのセルクリックでコマ移動
    }
    m_previzWindow->setScene(&activeCut().previz());
    m_previzWindow->setTimeline(m_currentFrame, activeCut().frameCount());
    m_previzWindow->show();
    m_previzWindow->raise();
    m_previzWindow->activateWindow();
}

void MainWindow::openStoryboardWindow() {
    if (!m_storyboardWindow) {
        m_storyboardWindow = new StoryboardWindow(this);  // QMainWindowなので独立ウィンドウになる
        connect(m_storyboardWindow, &StoryboardWindow::edited, this, [this] {
            markStoryboardDirty();
            updateWindowTitle();
        });
        // 「パネルからカット作成」: 選択パネルと同じカット番号を持つ全パネルのduration合計を尺として
        // 新規カットを作る(1カット複数コマの尺を合算する仕様)
        connect(m_storyboardWindow, &StoryboardWindow::createCutRequested, this,
                [this](const QString& cutLabel, int totalFrames) {
                    core::Scene& scene = m_project->scene(0);
                    core::Cut& cut = scene.addCut((QStringLiteral("カット ") + cutLabel).toStdString());
                    initializeCut(cut, canvasWidth(), canvasHeight());
                    cut.setFrameCount(static_cast<size_t>(std::max(1, totalFrames)));
                    markProjectDirty();  // シーンのカット構成(cutIds)が変わる
                    markCutDirty(cut);
                    updateWindowTitle();
                    updateCutBar();
                    setActiveCut(static_cast<int>(scene.cutCount() - 1));
                    refreshEditWindowIfOpen();
                });
    }
    m_storyboardWindow->setProject(m_project.get());
    m_storyboardWindow->refresh();
    m_storyboardWindow->show();
    m_storyboardWindow->raise();
    m_storyboardWindow->activateWindow();
}

void MainWindow::debugSetupStoryboardDemo() {
    // 絵コンテウィンドウ確認用: パネル2枚(共にカット番号"1")を追加し、パネル1のdrawing
    // (コンテ用紙全体を覆う1枚の手書きビットマップ)へ、絵の枠内に赤い斜め線、内容欄に
    // 青い線(効果音メモ想定)を描く。内容欄テキストは複数行入力の確認用に改行入りにする
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    panels.clear();

    // StoryboardWindow.cppの用紙レイアウトと同じ寸法(コンテ用紙全体)
    constexpr int kSheetWidth = 1920;
    constexpr int kSheetHeight = 600;
    // 画面(絵)欄内の16:9フレーム枠(kFrameRect相当)
    constexpr float kFrameX0 = 130.0f;
    constexpr float kFrameY0 = 30.0f;
    constexpr float kFrameX1 = 1090.0f;
    constexpr float kFrameY1 = 570.0f;
    // 内容欄
    constexpr float kActionX0 = 1100.0f;
    constexpr float kActionX1 = 1500.0f;

    core::StoryboardPanel panel1;
    panel1.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
    panel1.drawing.fill({0, 0, 0, 0});
    panel1.cutLabel = "1";
    panel1.action = "少年が走り出す\n(振り向きながら)";
    panel1.dialogue = "行くぞ!";
    panel1.durationFrames = 36;

    core::BrushEngine engine;
    engine.settings().radius = 8.0f;
    engine.settings().color = {220, 30, 30, 255};
    // 絵の枠内に赤い斜め線
    engine.beginStroke(panel1.drawing, kFrameX0 + (kFrameX1 - kFrameX0) * 0.2f,
                        kFrameY0 + (kFrameY1 - kFrameY0) * 0.2f, 1.0f);
    engine.continueStroke(panel1.drawing, kFrameX0 + (kFrameX1 - kFrameX0) * 0.8f,
                           kFrameY0 + (kFrameY1 - kFrameY0) * 0.8f, 1.0f);
    engine.endStroke();

    // 内容欄に青い線(効果音メモ想定)
    engine.settings().color = {30, 30, 220, 255};
    engine.beginStroke(panel1.drawing, kActionX0 + (kActionX1 - kActionX0) * 0.2f, kSheetHeight * 0.3f, 1.0f);
    engine.continueStroke(panel1.drawing, kActionX0 + (kActionX1 - kActionX0) * 0.8f, kSheetHeight * 0.7f, 1.0f);
    engine.endStroke();

    core::StoryboardPanel panel2;
    panel2.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
    panel2.drawing.fill({0, 0, 0, 0});
    panel2.cutLabel = "1";
    panel2.durationFrames = 12;

    panels.push_back(std::move(panel1));
    panels.push_back(std::move(panel2));
}

void MainWindow::openSettingBoardWindow() {
    if (!m_settingBoardWindow) {
        m_settingBoardWindow = new SettingBoardWindow(this);  // QMainWindowなので独立ウィンドウになる
        connect(m_settingBoardWindow, &SettingBoardWindow::edited, this, [this] {
            markBoardsDirty();
            updateWindowTitle();
            updateReferencePanel();  // 編集内容を参照ドックへ即反映
        });
    }
    m_settingBoardWindow->setProject(m_project.get());
    m_settingBoardWindow->refresh();
    m_settingBoardWindow->show();
    m_settingBoardWindow->raise();
    m_settingBoardWindow->activateWindow();
}

void MainWindow::updateReferencePanel() {
    if (!m_referencePanel) return;

    QStringList names;
    if (m_project) {
        for (const auto& board : m_project->settingBoards()) {
            names.append(QString::fromStdString(board.name));
        }
    }
    if (m_referenceBoardIndex < 0 || m_referenceBoardIndex >= names.size()) {
        m_referenceBoardIndex = names.isEmpty() ? -1 : 0;
    }
    m_referencePanel->setBoards(names, m_referenceBoardIndex);

    QImage image;
    QList<QPair<QString, QColor>> colorSpecs;
    if (m_project && m_referenceBoardIndex >= 0 &&
        static_cast<size_t>(m_referenceBoardIndex) < m_project->settingBoards().size()) {
        const core::SettingBoard& selectedBoard =
            m_project->settingBoards()[static_cast<size_t>(m_referenceBoardIndex)];
        if (!selectedBoard.image.isEmpty()) {
            // 参照パネルは表示専用のコピーを持つため、Bitmapの寿命/再配置に依存しないよう即コピーする
            image = QImage(selectedBoard.image.data(), selectedBoard.image.width(), selectedBoard.image.height(),
                            QImage::Format_RGBA8888)
                        .copy();
        }
        for (const core::ColorSpec& spec : selectedBoard.colorSpecs) {
            colorSpecs.append({QString::fromStdString(spec.name),
                               QColor(spec.color.r, spec.color.g, spec.color.b, spec.color.a)});
        }
    }
    m_referencePanel->setImage(image);
    m_referencePanel->setColorSpecs(colorSpecs);
}

void MainWindow::debugSetupSettingBoardDemo() {
    // 設定ボード確認用: ボード2枚追加(「キャラ: 主人公」「美術: 教室」)し、
    // 1枚目に赤い線を描いて参照ドックで1枚目を選択する
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    boards.clear();

    core::SettingBoard board1;
    board1.name = "キャラ: 主人公";
    board1.image = core::Bitmap(canvasWidth(), canvasHeight());  // 設定ボードウィンドウと同じ寸法(キャンバスサイズ)
    board1.image.fill({0, 0, 0, 0});

    core::BrushEngine engine;
    engine.settings().radius = 10.0f;
    engine.settings().color = {220, 30, 30, 255};
    engine.beginStroke(board1.image, canvasWidth() * 0.2f, canvasHeight() * 0.2f, 1.0f);
    engine.continueStroke(board1.image, canvasWidth() * 0.8f, canvasHeight() * 0.8f, 1.0f);
    engine.endStroke();

    // 色指定(色指定書): 「肌」「肌 影」「髪」の3色を見本として登録する
    board1.colorSpecs.push_back({"肌", {255, 224, 196, 255}});
    board1.colorSpecs.push_back({"肌 影", {233, 183, 150, 255}});
    board1.colorSpecs.push_back({"髪", {80, 60, 120, 255}});

    core::SettingBoard board2;
    board2.name = "美術: 教室";
    board2.image = core::Bitmap(canvasWidth(), canvasHeight());
    board2.image.fill({0, 0, 0, 0});

    boards.push_back(std::move(board1));
    boards.push_back(std::move(board2));

    m_referenceBoardIndex = 0;
    updateReferencePanel();
}

void MainWindow::openEditWindow() {
    if (!m_editWindow) {
        m_editWindow = new EditWindow(this);  // QMainWindowなので独立ウィンドウになる
        connect(m_editWindow, &EditWindow::edited, this, [this] {
            // 名前/尺/進捗/並べ替えのどれが変わったか信号からは分からないため、project+全カットの
            // 安全側で扱う(絵コンテ/設定ボードは対象外なのでmarkAllDirtyは使わない)
            markProjectDirty();
            m_dirtyScope.allCuts = true;
            updateWindowTitle();
            updateCutBar();  // カット名/構成が変わりうるためカットバーも追従させる
        });
        connect(m_editWindow, &EditWindow::cutActivated, this, [this](int index) { setActiveCut(index); });
    }
    m_editWindow->setCanvasSize(canvasWidth(), canvasHeight());
    m_editWindow->setProject(m_project.get());
    m_editWindow->refresh();
    m_editWindow->show();
    m_editWindow->raise();
    m_editWindow->activateWindow();
}

void MainWindow::refreshEditWindowIfOpen() {
    if (m_editWindow) m_editWindow->refresh();
    refreshShootingWindowIfOpen();  // カット構成の変更は撮影ウィンドウ(カット選択/シート)にも影響する
}

void MainWindow::openShootingWindow() {
    if (!m_shootingWindow) {
        m_shootingWindow = new ShootingWindow(this);  // QMainWindowなので独立ウィンドウになる
        connect(m_shootingWindow, &ShootingWindow::edited, this, [this] {
            // シグナルにカット特定が無く、メインウィンドウのアクティブカットと撮影ウィンドウの
            // 表示中カットが一致するとは限らないため安全側(全カット)で扱う
            m_dirtyScope.allCuts = true;
            m_dirty = true;
            updateWindowTitle();
            if (m_editWindow) m_editWindow->refresh();  // 通しプレビューのキャッシュを捨てて反映する
        });
    }
    m_shootingWindow->setCanvasSize(canvasWidth(), canvasHeight());
    m_shootingWindow->setProject(m_project.get());
    m_shootingWindow->setCutIndex(static_cast<int>(m_activeCut));
    m_shootingWindow->refresh();
    m_shootingWindow->show();
    m_shootingWindow->raise();
    m_shootingWindow->activateWindow();
}

void MainWindow::refreshShootingWindowIfOpen() {
    if (m_shootingWindow) m_shootingWindow->refresh();
}

void MainWindow::debugSetupEditDemo() {
    // 編集(カッティング)確認用: カット3つ(尺12/24/12、進捗: 原画/レイアウト/未着手)を組み、
    // カット1に赤ストローク・カット2に青ストロークを描いてから編集ウィンドウを開き、
    // グローバルコマ18(カット1=12コマの次、カット2内の6コマ目)へシークする
    if (!m_project || m_project->sceneCount() == 0) return;
    core::Scene& scene = m_project->scene(0);

    // 既存カットを消して3カット作り直す(先頭カットは既存の最小構成をそのまま使う)
    while (scene.cutCount() > 1) scene.removeCut(scene.cutCount() - 1);
    scene.cut(0).setName("カット 1");
    scene.cut(0).setFrameCount(12);
    scene.cut(0).setStatus(core::CutStatus::KeyAnimation);  // 原画
    // 動画1枚を全コマへ「止め」で割り付ける(尺を伸ばしても絵が消えないように)
    for (size_t t = 0; t < scene.cut(0).frameCount(); ++t) scene.cut(0).cel(0).setExposure(t, 0);

    core::Cut& cut2 = scene.addCut("カット 2");
    initializeCut(cut2, canvasWidth(), canvasHeight());
    cut2.setFrameCount(24);
    cut2.setStatus(core::CutStatus::Layout);  // レイアウト
    for (size_t t = 0; t < cut2.frameCount(); ++t) cut2.cel(0).setExposure(t, 0);

    core::Cut& cut3 = scene.addCut("カット 3");
    initializeCut(cut3, canvasWidth(), canvasHeight());
    cut3.setFrameCount(12);
    cut3.setStatus(core::CutStatus::NotStarted);  // 未着手
    for (size_t t = 0; t < cut3.frameCount(); ++t) cut3.cel(0).setExposure(t, 0);

    core::BrushEngine engine;
    engine.settings().radius = 10.0f;

    // カット1: 赤ストローク
    engine.settings().color = {220, 30, 30, 255};
    core::Bitmap& bmp1 = scene.cut(0).cel(0).layer(0).frame(0).bitmap();
    engine.beginStroke(bmp1, canvasWidth() * 0.2f, canvasHeight() * 0.2f, 1.0f);
    engine.continueStroke(bmp1, canvasWidth() * 0.8f, canvasHeight() * 0.8f, 1.0f);
    engine.endStroke();

    // カット2: 青ストローク(赤とは別の位置・向き)
    engine.settings().color = {30, 30, 220, 255};
    core::Bitmap& bmp2 = cut2.cel(0).layer(0).frame(0).bitmap();
    engine.beginStroke(bmp2, canvasWidth() * 0.8f, canvasHeight() * 0.2f, 1.0f);
    engine.continueStroke(bmp2, canvasWidth() * 0.2f, canvasHeight() * 0.8f, 1.0f);
    engine.endStroke();

    // 複数カットを作り直した上でストロークも描いており波及範囲が広いため安全側(全書き)に倒す
    markAllDirty();
    updateWindowTitle();
    updateCutBar();
    setActiveCut(0);

    openEditWindow();
    // グローバルコマ18 = カット1(尺12)を過ぎた後のカット2内コマ6(0始まりなら6コマ目=index6)
    m_editWindow->debugSeekToGlobalFrame(18);
}

void MainWindow::openExportDialog() {
    if (m_playing) return;  // 再生中は書き出しできない

    core::Cut& cut = activeCut();
    QStringList celNames;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        celNames.append(QString::fromStdString(cut.cel(ci).name()));
    }

    ExportDialog dialog(celNames, static_cast<int>(cut.frameCount()), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString outputPath = dialog.outputPath();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("書き出しエラー"), tr("出力先を指定してください"));
        return;
    }

    core::RenderOptions opts;
    opts.includeColorTrace = dialog.includeColorTrace();
    opts.includeCorrection = dialog.includeCorrection();
    opts.onlyCel = dialog.onlyCel();
    opts.useExportSamples = true;  // 書き出しはクラシック撮影を高サンプルでなめらかに

    // ダイアログのコマ番号は1始まり、renderCutFrame等の内部は0始まりなので変換する
    const int from = dialog.fromFrame() - 1;
    const int to = dialog.toFrame() - 1;

    const bool success = dialog.format() == ExportDialog::Format::Sequence
                              ? exportSequence(outputPath, from, to, opts)
                              : exportMovie(outputPath, from, to, dialog.fps(), opts);

    if (success) {
        statusBar()->showMessage(tr("書き出しました: %1").arg(outputPath), 5000);
    }
}

bool MainWindow::exportSequence(const QString& dir, int from, int to, const core::RenderOptions& opts) {
    QDir outDir(dir);
    if (!outDir.exists() && !QDir().mkpath(dir)) return false;

    const int total = to - from + 1;
    if (total <= 0) return false;

    core::Cut& cut = activeCut();

    // mp4書き出し(libx264+yuv420p)は奇数解像度だと失敗するため、書き出しサイズは常に2の倍数へ
    // 切り下げる(連番PNG単体の書き出しでも同じサイズに揃えることで挙動を一貫させる)
    const int outW = canvasWidth() & ~1;
    const int outH = canvasHeight() & ~1;

    QProgressDialog progress(tr("書き出し中..."), tr("キャンセル"), 0, total, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    for (int i = 0; i < total; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return false;

        const size_t frame = static_cast<size_t>(from + i);
        const core::Bitmap bitmap = core::renderCutFrame(cut, frame, outW, outH, opts);
        // コピーせずbitmapのデータへ直接QImageを被せてsave()する(bitmapの寿命内なので安全)
        const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
        const QString path = outDir.filePath(QStringLiteral("frame_%1.png").arg(i + 1, 4, 10, QChar('0')));
        if (!image.save(path)) return false;
    }
    progress.setValue(total);
    return true;
}

bool MainWindow::exportMovie(const QString& mp4Path, int from, int to, int fps, const core::RenderOptions& opts) {
    // 連番PNGを一時フォルダに書き出してからffmpegでエンコードする
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) return false;
    if (!exportSequence(tempDir.path(), from, to, opts)) return false;

    const QString framePattern = QDir(tempDir.path()).filePath(QStringLiteral("frame_%04d.png"));

    // ffmpegを実行し、成功したか/失敗時のstderrを返す
    const auto runFfmpeg = [&](const QStringList& codecArgs) -> std::pair<bool, QString> {
        QStringList args;
        args << QStringLiteral("-y") << QStringLiteral("-framerate") << QString::number(fps) << QStringLiteral("-i")
             << framePattern;
        args += codecArgs;
        args << mp4Path;

        QProcess process;
        process.start(QStringLiteral("ffmpeg"), args);
        if (!process.waitForFinished(120000)) {  // 無制限(-1)ではなく120秒でタイムアウトさせる
            process.kill();
            return {false, tr("ffmpegの実行がタイムアウトしました")};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return {false, QString::fromLocal8Bit(process.readAllStandardError())};
        }
        return {true, QString()};
    };

    auto [ok, err] = runFfmpeg({QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                                 QStringLiteral("yuv420p")});
    if (!ok) {
        // libx264が無いLGPLビルドの可能性があるため、mpeg4で再試行する
        auto [ok2, err2] = runFfmpeg({QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("3")});
        if (!ok2) {
            QMessageBox::warning(this, tr("書き出しエラー"), tr("動画の書き出しに失敗しました:\n%1").arg(err2.left(500)));
            return false;
        }
    }
    return true;
}

bool MainWindow::exportAllCutsMovie(const QString& mp4Path, int fps) {
    if (!m_project || m_project->sceneCount() == 0) return false;
    core::Scene& scene = m_project->scene(0);
    if (scene.cutCount() == 0) return false;

    const QFileInfo mp4Info(mp4Path);
    const QString outDir = mp4Info.absolutePath();
    QDir().mkpath(outDir);

    // 連番PNGの書き出し先(固定フォルダ: mp4と同じ場所の"<ベース名>_frames")。
    // ffmpegが無い/失敗した場合でも中身を残し、目視確認に使えるようにする
    const QString framesDir = QDir(outDir).filePath(mp4Info.completeBaseName() + QStringLiteral("_frames"));
    QDir().mkpath(framesDir);
    {
        // 再実行時に古いコマが混在しないよう、既存の連番PNGを先に削除する
        QDir dir(framesDir);
        for (const QString& name : dir.entryList(QStringList() << QStringLiteral("frame_*.png"), QDir::Files)) {
            dir.remove(name);
        }
    }

    core::RenderOptions opts;  // 既定=最終画(色トレス線/作監修正は含めない)
    opts.useExportSamples = true;  // 書き出しはクラシック撮影を高サンプルでなめらかに

    // mp4書き出し(libx264+yuv420p)は奇数解像度だと失敗するため、書き出しサイズは常に2の倍数へ切り下げる
    const int outW = canvasWidth() & ~1;
    const int outH = canvasHeight() & ~1;

    int globalFrame = 0;
    for (size_t ci = 0; ci < scene.cutCount(); ++ci) {
        core::Cut& cut = scene.cut(ci);
        const size_t frameCount = cut.frameCount();
        if (frameCount == 0) continue;
        const size_t midFrame = (frameCount - 1) / 2;  // 代表フレーム(カットの中間コマ)

        for (size_t f = 0; f < frameCount; ++f) {
            const core::Bitmap bitmap = core::renderCutFrame(cut, f, outW, outH, opts);
            const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);

            ++globalFrame;
            const QString framePath =
                QDir(framesDir).filePath(QStringLiteral("frame_%1.png").arg(globalFrame, 4, 10, QChar('0')));
            image.save(framePath);

            if (f == midFrame) {
                // 代表フレームはmp4の成否によらず必ず保存する(目視確認用)
                const QString repPath =
                    QDir(outDir).filePath(QStringLiteral("fulldemo_cut%1.png").arg(ci + 1));
                image.save(repPath);
            }
        }
    }
    if (globalFrame == 0) return false;

    // ffmpegでframe_%04d.png連番→mp4へエンコード(exportMovieと同じ規則、ダイアログは出さない)
    const QString framePattern = QDir(framesDir).filePath(QStringLiteral("frame_%04d.png"));
    const auto runFfmpeg = [&](const QStringList& codecArgs) -> bool {
        QStringList args;
        args << QStringLiteral("-y") << QStringLiteral("-framerate") << QString::number(fps) << QStringLiteral("-i")
             << framePattern;
        args += codecArgs;
        args << mp4Path;

        QProcess process;
        process.start(QStringLiteral("ffmpeg"), args);
        // 全カット分(尺108コマ)をエンコードするため、単体エクスポートより長めのタイムアウトにする
        if (!process.waitForFinished(300000)) {
            process.kill();
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };

    if (runFfmpeg({QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                    QStringLiteral("yuv420p")})) {
        return true;
    }
    // libx264が無いLGPLビルドの可能性があるため、mpeg4で再試行する
    return runFfmpeg({QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("3")});
}

int MainWindow::debugExportSequence(const QString& dir) {
    debugSetupXsheetDemo();  // 尺6・2コマ打ちのデモを組む
    core::Cut& cut = activeCut();
    const core::RenderOptions opts;
    return exportSequence(dir, 0, static_cast<int>(cut.frameCount()) - 1, opts) ? 0 : 1;
}

void MainWindow::checkAutosaveRecovery() {
    const QString path = autosavePath();
    // 存在判定はフォルダ自体ではなくproject.ppamの有無で行う(空フォルダ・不完全な残骸を誤検知しない)
    if (!QFileInfo::exists(QDir(path).filePath(QStringLiteral("project.ppam")))) return;

    const auto reply =
        QMessageBox::question(this, tr("自動保存データの復元"), tr("前回のセッションの自動保存データが見つかりました。復元しますか？"),
                               QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (loadFromFile(path)) {
            m_currentFilePath.clear();  // 復元後は名前を付けて保存を促す
            markAllDirty();  // 復元直後は保存先が無いため、次の保存(名前を付けて保存)で全書きする
            updateWindowTitle();
        }
    } else {
        QDir(path).removeRecursively();  // フォルダごと削除する
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_dirty) {
        const auto reply = QMessageBox::question(this, tr("確認"), tr("保存されていない変更があります。保存しますか？"),
                                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (reply == QMessageBox::Save) {
            save();
            if (m_dirty) {
                // 名前を付けて保存がキャンセルされた等で保存が完了しなかった場合はクローズしない
                event->ignore();
                return;
            }
        }
        // Discardの場合は変更を破棄してそのままクローズする
    }

    QDir(autosavePath()).removeRecursively();  // 正常終了なのでリカバリ用データ(フォルダ)は不要
    event->accept();
}
