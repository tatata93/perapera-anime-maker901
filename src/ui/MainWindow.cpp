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
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
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
#include "ui/CelPanel.h"
#include "ui/CelSizeDialog.h"
#include "ui/EditWindow.h"
#include "ui/EffectPanel.h"
#include "ui/ExportDialog.h"
#include "ui/FramePanel.h"
#include "ui/LayerPanel.h"
#include "ui/PalettePanel.h"
#include "ui/ReferencePanel.h"
#include "ui/SettingBoardWindow.h"
#include "ui/StoryboardWindow.h"
#include "ui/TapPanel.h"
#include "ui/XsheetPanel.h"

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
constexpr int kDefaultFps = 24;  // タイムシートは24fps基準

// 自動保存: ファイル名と保存間隔(3分)
const QString kAutosaveFileName = QStringLiteral("autosave.ppam");
constexpr int kAutosaveIntervalMs = 180 * 1000;

// 透明なセル(作画用紙)。紙の白はGLCanvasが背景として描画する。
// width/heightを省略するとキャンバスサイズになる
core::Bitmap makeTransparentCel(int width = kCanvasWidth, int height = kCanvasHeight) {
    core::Bitmap bitmap(width, height);
    bitmap.fill({0, 0, 0, 0});
    return bitmap;
}

// 引きセル対応: 指定セルの用紙サイズ(0ならキャンバスサイズ)で透明ビットマップを作る。
// 既存セルへ動画やレイヤーを追加する際、既にリサイズ済みの用紙サイズに合わせるために使う
core::Bitmap makeTransparentCelForCel(const core::Cel& cel) {
    const int w = cel.paperWidth() > 0 ? cel.paperWidth() : kCanvasWidth;
    const int h = cel.paperHeight() > 0 ? cel.paperHeight() : kCanvasHeight;
    return makeTransparentCel(w, h);
}
// 新規カットの最小構成(セルA+レイヤー1+動画1、尺1コマ)を作る
void initializeCut(core::Cut& cut) {
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("レイヤー 1");
    layer.addFrame().bitmap() = makeTransparentCel();
    cut.setFrameCount(1);
    cel.setExposure(0, 0);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("perapera-anime-maker901");

    m_canvas = new GLCanvas(this);
    m_canvas->setCanvasSize(kCanvasWidth, kCanvasHeight);
    setCentralWidget(m_canvas);
    m_canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command> command) {
        m_commands.push(std::move(command));  // pushは冪等なexecute(after画素の再書き込み)を伴う
        m_dirty = true;
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
}

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("カット 1");
    initializeCut(cut);

    m_currentFrame = 0;
    m_activeCut = 0;
    m_activeCel = 0;
    m_activeLayer = 0;
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
        cel.layer(li).addFrame().bitmap() = makeTransparentCelForCel(cel);
    }
    const bool currentIsEmpty = cel.exposure(m_currentFrame) == -1;
    const size_t target = currentIsEmpty ? m_currentFrame : m_currentFrame + 1;
    core::Cut& cut = activeCut();
    if (target >= cut.frameCount()) cut.setFrameCount(target + 1);
    cel.setExposure(target, newDrawing);

    m_commands.clear();             // 構造変更のためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 動画構造が変わったため
    setCurrentFrame(target);
    m_dirty = true;
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
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::togglePlayback() {
    m_playing = !m_playing;
    if (m_playing) {
        m_playAction->setText(tr("停止"));
        m_canvas->setInputEnabled(false);
        updateOnionSkin();          // 再生中はオニオンスキンを消す
        m_canvas->setLightTable({});  // 再生中はライトテーブルも消す
        m_playTimer->start(1000 / std::max(1, m_fpsSpin->value()));
    } else {
        m_playTimer->stop();
        m_playAction->setText(tr("再生"));
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
        m_canvas->setUnderlayImage(m_previzWindow->viewport()->renderCameraViewImage());
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
    initializeCut(cut);
    m_dirty = true;
    updateWindowTitle();
    setActiveCut(static_cast<int>(scene.cutCount() - 1));
    refreshEditWindowIfOpen();
}

void MainWindow::removeActiveCut() {
    if (m_playing) return;
    core::Scene& scene = m_project->scene(0);
    if (scene.cutCount() <= 1) return;  // 最後の1カットは消さない
    scene.removeCut(m_activeCut);
    m_dirty = true;
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
    m_dirty = true;
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
        m_dirty = true;
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
        m_dirty = true;
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

        m_dirty = true;
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
        m_dirty = true;
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
        m_dirty = true;
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
        m_dirty = true;
        updateCanvasLayers();
        updateOnionSkin();
        updateFrameLabel();
        updateXsheetPanel();
        updateWindowTitle();
    });
    connect(m_xsheetPanel, &XsheetPanel::stepPatternRequested, this, [this](int step) {
        if (m_playing) return;
        activeCel().applyStepPattern(step, activeCut().frameCount());
        m_dirty = true;
        updateCanvasLayers();
        updateOnionSkin();
        updateFrameLabel();
        updateXsheetPanel();
        updateWindowTitle();
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
        m_dirty = true;
        updateCanvasLayers();
        updateWindowTitle();
        updateTapPanel();
    });
    connect(m_tapPanel, &TapPanel::removeKeyRequested, this, [this](int frame) {
        if (m_playing) return;
        activeCel().removePositionKey(static_cast<size_t>(frame));
        m_dirty = true;
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
        m_dirty = true;
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
        m_dirty = true;
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::removeKeyRequested, this, [this] {
        if (m_playing) return;
        activeCut().removeCameraKey(m_currentFrame);
        m_dirty = true;
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::clearAllKeysRequested, this, [this] {
        if (m_playing) return;
        activeCut().clearCameraKeys();
        m_dirty = true;
        updateWindowTitle();
        updateCameraPanel();
    });
    connect(m_cameraPanel, &CameraPanel::showFrameToggled, this, [this](bool) { updateCameraOverlay(); });

    m_effectPanel = new EffectPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_effectPanel);
    connect(m_effectPanel, &EffectPanel::effectsEdited, this, [this] {
        activeCut().effects() = m_effectPanel->effects();
        m_dirty = true;
        updateWindowTitle();
        refreshEditWindowIfOpen();  // プレビューキャッシュを捨てて反映する
    });
    connect(m_effectPanel, &EffectPanel::previewRequested, this, &MainWindow::showEffectPreview);

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
        layer.addFrame().bitmap() = makeTransparentCelForCel(cel);  // 既存レイヤーとコマ数(・用紙サイズ)を揃える
    }
    m_activeLayer = cel.layerCount() - 1;
    m_commands.clear();
    m_canvas->clearTextureCache();
    m_dirty = true;
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
    m_dirty = true;
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
    m_dirty = true;
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
    m_dirty = true;
    updatePalettePanel();
    updateWindowTitle();
}

void MainWindow::removeSelectedPaletteColor() {
    if (!m_palettePanel) return;
    const int index = m_palettePanel->selectedIndex();
    auto& palette = m_project->palette();
    if (index < 0 || static_cast<size_t>(index) >= palette.size()) return;
    palette.erase(palette.begin() + index);
    m_dirty = true;
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
    updateEffectPanel();  // 撮影エフェクトのスタック(カット単位)も現在カットに追従させる
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
        m_cameraPanel->setValues(kCanvasWidth / 2.0, kCanvasHeight / 2.0, 100.0);
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
    const double w = kCanvasWidth * scale;
    const double h = kCanvasHeight * scale;
    const double cx = m_cameraPanel->centerX();
    const double cy = m_cameraPanel->centerY();
    m_canvas->setCameraFrameOverlay(QRectF(cx - w / 2.0, cy - h / 2.0, w, h));
}

// アクティブカットのエフェクトスタックとセル名一覧を撮影パネルへ反映する
void MainWindow::updateEffectPanel() {
    if (!m_effectPanel) return;
    core::Cut& cut = activeCut();

    QStringList celNames;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) celNames.append(QString::fromStdString(cut.cel(ci).name()));

    m_effectPanel->setEffects(cut.effects(), celNames);
}

// 撮影パネルの「現在コマをプレビュー」要求: activeCut()の現在コマをエフェクト適用込みで
// renderCutFrameし、撮影パネルのプレビューダイアログへ渡す
void MainWindow::showEffectPreview() {
    if (!m_effectPanel) return;
    const core::Bitmap bitmap = core::renderCutFrame(activeCut(), m_currentFrame, kCanvasWidth, kCanvasHeight);
    const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    // showPreview内でQPixmapへ変換(ピクセルデータを複製)するため、bitmap(ローカル変数)の寿命中に呼べば安全
    m_effectPanel->showPreview(image);
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
    layer.addFrame().bitmap() = makeTransparentCel();
    cel.setExposure(m_currentFrame, 0);  // 現在コマに動画1を割り付け、すぐ描ける状態にする
    m_activeCel = static_cast<size_t>(index);

    // 新規セル・レイヤー・動画は既存のBitmapに影響しないためUndo履歴/テクスチャキャッシュの破棄は不要
    m_dirty = true;
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
    m_dirty = true;
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

    m_dirty = true;
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

    CelSizeDialog dialog(cel.paperWidth(), cel.paperHeight(), kCanvasWidth, kCanvasHeight, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int newW = dialog.paperWidth();
    const int newH = dialog.paperHeight();
    // 現在有効なサイズ(0ならキャンバスサイズ)と比較し、実質変更なしなら何もしない
    const int effectiveW = cel.paperWidth() > 0 ? cel.paperWidth() : kCanvasWidth;
    const int effectiveH = cel.paperHeight() > 0 ? cel.paperHeight() : kCanvasHeight;
    if (newW == effectiveW && newH == effectiveH) return;

    cel.resizePaper(newW, newH);

    m_commands.clear();             // 全ビットマップが再確保されたためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 同上、テクスチャキャッシュも破棄
    m_dirty = true;
    updateCanvasLayers();
    updateOnionSkin();
    updateWindowTitle();
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
    m_dirty = true;
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
    m_dirty = true;
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
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::setCelVisibility(int celIndex, bool visible) {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;

    cut.cel(static_cast<size_t>(celIndex)).setVisible(visible);

    m_dirty = true;
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
}

void MainWindow::redo() {
    if (m_playing || !m_commands.canRedo()) return;
    core::Command* command = m_commands.redo();
    if (auto* stroke = dynamic_cast<core::StrokeCommand*>(command)) {
        m_canvas->notifyBitmapRegionChanged(stroke->bitmap(), stroke->region());
    } else if (command) {
        m_canvas->clearTextureCache();
    }
}

void MainWindow::updateWindowTitle() {
    const QString base = QStringLiteral("perapera-anime-maker901");
    QString title = m_currentFilePath.isEmpty() ? base : QStringLiteral("%1 - %2").arg(base, QFileInfo(m_currentFilePath).fileName());
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
    m_referenceBoardIndex = -1;
    updateReferencePanel();
    updateFrameLabel();
    updateLayerPanel();
    updatePalettePanel();
    updateXsheetPanel();
    m_currentFilePath.clear();
    m_dirty = false;
    updateWindowTitle();
}

bool MainWindow::saveToFile(const QString& path) {
    std::string error;
    if (!core::ProjectIO::save(*m_project, std::filesystem::path(path.toStdWString()), &error)) {
        QMessageBox::warning(this, tr("保存エラー"), QString::fromStdString(error));
        return false;
    }
    m_currentFilePath = path;
    m_dirty = false;
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
    m_referenceBoardIndex = -1;
    updateReferencePanel();
    setCurrentFrame(0);
    updateLayerPanel();
    updateCutBar();
    updatePalettePanel();
    updateXsheetPanel();
    m_currentFilePath = path;
    m_dirty = false;
    updateWindowTitle();
    return true;
}

void MainWindow::save() {
    if (m_currentFilePath.isEmpty()) {
        saveAs();
    } else {
        saveToFile(m_currentFilePath);
    }
}

void MainWindow::saveAs() {
    const QString path =
        QFileDialog::getSaveFileName(this, tr("プロジェクトを保存"), QString(), tr("ぺらぺらプロジェクト (*.ppam)"));
    if (!path.isEmpty()) saveToFile(path);
}

void MainWindow::open() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("プロジェクトを開く"), QString(), tr("ぺらぺらプロジェクト (*.ppam)"));
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
    addAction->setShortcut(QKeySequence(Qt::Key_A));
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
    m_playAction = toolBar->addAction(tr("再生"));
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
}

void MainWindow::debugSetupOnionDemo() {
    // 3フレームにそれぞれ位置の異なる縦線を描き、中央フレームを表示する
    core::Layer& layer = activeLayer();
    while (layer.frameCount() < 3) {
        core::Frame& frame = layer.addFrame();
        frame.bitmap() = makeTransparentCel();
    }

    core::BrushEngine engine;
    engine.settings().radius = 14.0f;
    engine.settings().color = {0, 0, 0, 255};
    for (int i = 0; i < 3; ++i) {
        core::Bitmap& bitmap = layer.frame(static_cast<size_t>(i)).bitmap();
        const float x = kCanvasWidth * (0.30f + 0.20f * static_cast<float>(i));
        engine.beginStroke(bitmap, x, kCanvasHeight * 0.25f, 0.9f);
        engine.continueStroke(bitmap, x, kCanvasHeight * 0.75f, 0.9f);
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
    engine.beginStroke(bottom, kCanvasWidth * 0.4f, kCanvasHeight * 0.25f, 0.9f);
    engine.continueStroke(bottom, kCanvasWidth * 0.4f, kCanvasHeight * 0.75f, 0.9f);
    engine.endStroke();

    core::Bitmap& top = cel.layer(1).frame(m_currentFrame).bitmap();
    engine.settings().color = {40, 40, 200, 255};
    engine.beginStroke(top, kCanvasWidth * 0.25f, kCanvasHeight * 0.5f, 0.9f);
    engine.continueStroke(top, kCanvasWidth * 0.75f, kCanvasHeight * 0.5f, 0.9f);
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
    cel.resizePaper(kCanvasWidth * 2, kCanvasHeight);
    m_canvas->clearTextureCache();

    // 用紙全域(縦方向は上〜下いっぱい)に、左半分=赤・右半分=青の縦線を描く
    core::Bitmap& bitmap = activeLayer().frame(0).bitmap();
    core::BrushEngine engine;
    engine.settings().radius = 20.0f;

    engine.settings().color = {220, 30, 30, 255};  // 左半分: 赤
    const float leftX = kCanvasWidth * 0.5f;
    engine.beginStroke(bitmap, leftX, kCanvasHeight * 0.1f, 1.0f);
    engine.continueStroke(bitmap, leftX, kCanvasHeight * 0.9f, 1.0f);
    engine.endStroke();

    engine.settings().color = {30, 60, 220, 255};  // 右半分: 青
    const float rightX = kCanvasWidth * 1.5f;
    engine.beginStroke(bitmap, rightX, kCanvasHeight * 0.1f, 1.0f);
    engine.continueStroke(bitmap, rightX, kCanvasHeight * 0.9f, 1.0f);
    engine.endStroke();

    // 位置キー: コマ0=オフセット0(左半分の赤がキャンバス内)、
    // コマ2=左へキャンバス幅ぶんパン(セルが左へずれ、右半分の青がキャンバス内に来る)
    core::Cut& cut = activeCut();
    cut.setFrameCount(3);
    for (size_t t = 0; t < 3; ++t) cel.setExposure(t, 0);  // 止め(同じ動画をコマ0〜2に表示)
    cel.setPositionKey(0, {0.0f, 0.0f});
    cel.setPositionKey(2, {static_cast<float>(-kCanvasWidth), 0.0f});

    m_dirty = true;
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
    cut.setCameraKey(0, core::CameraFrameState{{kCanvasWidth / 2.0f, kCanvasHeight / 2.0f}, 1.0});
    cut.setCameraKey(23, core::CameraFrameState{{kCanvasWidth * 0.3f, kCanvasHeight * 0.3f}, 0.5});
    m_dirty = true;
    updateWindowTitle();
    setCurrentFrame(12);
}

void MainWindow::debugSetupEffectsDemo() {
    // 撮影パネル確認用: ストローク1本(矩形枠、セル0=アクティブセルに描く)を描いてから、
    // 全体ブラー(半径6)・全体パラ(既定)・セル0対象グローの3エフェクトを追加し、
    // 撮影プレビューダイアログを開く
    debugSetupFillDemo();  // 目印になる矩形枠をアクティブセル(セル0)に描く

    core::Cut& cut = activeCut();
    cut.effects().clear();

    core::Effect blur;
    blur.type = core::EffectType::Blur;
    blur.enabled = true;
    blur.targetCel = -1;  // 全体
    blur.params = core::effectDefaultParams(core::EffectType::Blur);
    blur.params["radius"] = 6.0;
    cut.effects().push_back(blur);

    core::Effect para;
    para.type = core::EffectType::Para;
    para.enabled = true;
    para.targetCel = -1;  // 全体
    para.params = core::effectDefaultParams(core::EffectType::Para);
    cut.effects().push_back(para);

    core::Effect glow;
    glow.type = core::EffectType::Glow;
    glow.enabled = true;
    glow.targetCel = 0;  // セル0対象
    glow.params = core::effectDefaultParams(core::EffectType::Glow);
    cut.effects().push_back(glow);

    m_dirty = true;
    updateWindowTitle();
    updateEffectPanel();
    showEffectPreview();
}

void MainWindow::debugSetupFillDemo() {
    // 閉じた矩形枠(黒)を現在フレームのアクティブレイヤーに描く
    core::Bitmap& bitmap = activeLayer().frame(m_currentFrame).bitmap();
    core::BrushEngine engine;
    engine.settings().radius = 8.0f;
    engine.settings().color = {0, 0, 0, 255};

    const float x0 = kCanvasWidth * 0.30f, x1 = kCanvasWidth * 0.70f;
    const float y0 = kCanvasHeight * 0.30f, y1 = kCanvasHeight * 0.70f;
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
    engine.beginStroke(trace, kCanvasWidth * 0.5f, kCanvasHeight * 0.25f, 1.0f);
    engine.continueStroke(trace, kCanvasWidth * 0.5f, kCanvasHeight * 0.75f, 1.0f);
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
    engine.beginStroke(bitmapA, kCanvasWidth * 0.4f, kCanvasHeight * 0.25f, 0.9f);
    engine.continueStroke(bitmapA, kCanvasWidth * 0.4f, kCanvasHeight * 0.75f, 0.9f);
    engine.endStroke();

    core::Cel& celB = cut.cel(1);
    core::Bitmap& bitmapB = celB.layer(0).frame(0).bitmap();
    engine.settings().color = {40, 40, 200, 255};
    engine.beginStroke(bitmapB, kCanvasWidth * 0.25f, kCanvasHeight * 0.5f, 0.9f);
    engine.continueStroke(bitmapB, kCanvasWidth * 0.75f, kCanvasHeight * 0.5f, 0.9f);
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

int MainWindow::debugPaletteRoundTrip(const QString& ppamPath) {
    // パレットに3色を追加→保存→新規(パレット空になる)→読込を行い、往復結果を検証する
    const core::Bitmap::Pixel expected[3] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    m_project->palette().clear();
    for (const core::Bitmap::Pixel& color : expected) {
        m_project->palette().push_back(color);
    }

    if (!saveToFile(ppamPath)) return 1;
    newDocument();  // パレットが空に戻ることを確認する前提の状態にする
    if (!loadFromFile(ppamPath)) return 1;

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

int MainWindow::debugRoleRoundTrip(const QString& ppamPath) {
    // レイヤーを2枚追加(計3枚)し、layer(1)をColorTrace、layer(2)をCorrectionに設定して
    // 保存→新規→読込を行い、種別が保持されているか検証する
    addLayerToActiveCel();  // レイヤー2枚目
    addLayerToActiveCel();  // レイヤー3枚目

    core::Cel& cel = activeCel();
    if (cel.layerCount() != 3) return 1;
    cel.layer(1).setRole(core::LayerRole::ColorTrace);
    cel.layer(2).setRole(core::LayerRole::Correction);

    if (!saveToFile(ppamPath)) return 1;
    newDocument();  // 白紙に戻す
    if (!loadFromFile(ppamPath)) return 1;

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
            m_dirty = true;
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
            m_dirty = true;
            updateWindowTitle();
        });
        // 「パネルからカット作成」: 選択パネルと同じカット番号を持つ全パネルのduration合計を尺として
        // 新規カットを作る(1カット複数コマの尺を合算する仕様)
        connect(m_storyboardWindow, &StoryboardWindow::createCutRequested, this,
                [this](const QString& cutLabel, int totalFrames) {
                    core::Scene& scene = m_project->scene(0);
                    core::Cut& cut = scene.addCut((QStringLiteral("カット ") + cutLabel).toStdString());
                    initializeCut(cut);
                    cut.setFrameCount(static_cast<size_t>(std::max(1, totalFrames)));
                    m_dirty = true;
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
            m_dirty = true;
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
    board1.image = core::Bitmap(kCanvasWidth, kCanvasHeight);  // 設定ボードウィンドウと同じ寸法(1920x1080)
    board1.image.fill({0, 0, 0, 0});

    core::BrushEngine engine;
    engine.settings().radius = 10.0f;
    engine.settings().color = {220, 30, 30, 255};
    engine.beginStroke(board1.image, kCanvasWidth * 0.2f, kCanvasHeight * 0.2f, 1.0f);
    engine.continueStroke(board1.image, kCanvasWidth * 0.8f, kCanvasHeight * 0.8f, 1.0f);
    engine.endStroke();

    // 色指定(色指定書): 「肌」「肌 影」「髪」の3色を見本として登録する
    board1.colorSpecs.push_back({"肌", {255, 224, 196, 255}});
    board1.colorSpecs.push_back({"肌 影", {233, 183, 150, 255}});
    board1.colorSpecs.push_back({"髪", {80, 60, 120, 255}});

    core::SettingBoard board2;
    board2.name = "美術: 教室";
    board2.image = core::Bitmap(kCanvasWidth, kCanvasHeight);
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
            m_dirty = true;
            updateWindowTitle();
            updateCutBar();  // カット名/構成が変わりうるためカットバーも追従させる
        });
        connect(m_editWindow, &EditWindow::cutActivated, this, [this](int index) { setActiveCut(index); });
    }
    m_editWindow->setCanvasSize(kCanvasWidth, kCanvasHeight);
    m_editWindow->setProject(m_project.get());
    m_editWindow->refresh();
    m_editWindow->show();
    m_editWindow->raise();
    m_editWindow->activateWindow();
}

void MainWindow::refreshEditWindowIfOpen() {
    if (m_editWindow) m_editWindow->refresh();
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
    initializeCut(cut2);
    cut2.setFrameCount(24);
    cut2.setStatus(core::CutStatus::Layout);  // レイアウト
    for (size_t t = 0; t < cut2.frameCount(); ++t) cut2.cel(0).setExposure(t, 0);

    core::Cut& cut3 = scene.addCut("カット 3");
    initializeCut(cut3);
    cut3.setFrameCount(12);
    cut3.setStatus(core::CutStatus::NotStarted);  // 未着手
    for (size_t t = 0; t < cut3.frameCount(); ++t) cut3.cel(0).setExposure(t, 0);

    core::BrushEngine engine;
    engine.settings().radius = 10.0f;

    // カット1: 赤ストローク
    engine.settings().color = {220, 30, 30, 255};
    core::Bitmap& bmp1 = scene.cut(0).cel(0).layer(0).frame(0).bitmap();
    engine.beginStroke(bmp1, kCanvasWidth * 0.2f, kCanvasHeight * 0.2f, 1.0f);
    engine.continueStroke(bmp1, kCanvasWidth * 0.8f, kCanvasHeight * 0.8f, 1.0f);
    engine.endStroke();

    // カット2: 青ストローク(赤とは別の位置・向き)
    engine.settings().color = {30, 30, 220, 255};
    core::Bitmap& bmp2 = cut2.cel(0).layer(0).frame(0).bitmap();
    engine.beginStroke(bmp2, kCanvasWidth * 0.8f, kCanvasHeight * 0.2f, 1.0f);
    engine.continueStroke(bmp2, kCanvasWidth * 0.2f, kCanvasHeight * 0.8f, 1.0f);
    engine.endStroke();

    m_dirty = true;
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

    QProgressDialog progress(tr("書き出し中..."), tr("キャンセル"), 0, total, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    for (int i = 0; i < total; ++i) {
        progress.setValue(i);
        if (progress.wasCanceled()) return false;

        const size_t frame = static_cast<size_t>(from + i);
        const core::Bitmap bitmap = core::renderCutFrame(cut, frame, kCanvasWidth, kCanvasHeight, opts);
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

int MainWindow::debugExportSequence(const QString& dir) {
    debugSetupXsheetDemo();  // 尺6・2コマ打ちのデモを組む
    core::Cut& cut = activeCut();
    const core::RenderOptions opts;
    return exportSequence(dir, 0, static_cast<int>(cut.frameCount()) - 1, opts) ? 0 : 1;
}

void MainWindow::checkAutosaveRecovery() {
    const QString path = autosavePath();
    if (!QFileInfo::exists(path)) return;

    const auto reply =
        QMessageBox::question(this, tr("自動保存データの復元"), tr("前回のセッションの自動保存データが見つかりました。復元しますか？"),
                               QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (loadFromFile(path)) {
            m_currentFilePath.clear();  // 復元後は名前を付けて保存を促す
            m_dirty = true;
            updateWindowTitle();
        }
    } else {
        QFile::remove(path);
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

    QFile::remove(autosavePath());  // 正常終了なのでリカバリ用データは不要
    event->accept();
}
