#include "MainWindow.h"

#include <QActionGroup>
#include <QCloseEvent>
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
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <filesystem>

#include "core/BrushEngine.h"
#include "core/ProjectIO.h"
#include "core/StrokeCommand.h"
#include "render/GLCanvas.h"
#include "ui/FramePanel.h"
#include "ui/LayerPanel.h"
#include "ui/PalettePanel.h"
#include "ui/XsheetPanel.h"

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
constexpr int kDefaultFps = 24;  // タイムシートは24fps基準

// 自動保存: ファイル名と保存間隔(3分)
const QString kAutosaveFileName = QStringLiteral("autosave.ppam");
constexpr int kAutosaveIntervalMs = 180 * 1000;

// 透明なセル(作画用紙)。紙の白はGLCanvasが背景として描画する
core::Bitmap makeTransparentCel() {
    core::Bitmap bitmap(kCanvasWidth, kCanvasHeight);
    bitmap.fill({0, 0, 0, 0});
    return bitmap;
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
    // MVPでは1シーン・1カット固定
    return m_project->scene(0).cut(0);
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
    std::vector<const core::Bitmap*> stack;
    std::vector<const core::Bitmap*> fillBoundary;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        core::Cel& cel = cut.cel(ci);
        if (!cel.visible()) continue;
        const int drawing = cel.exposure(m_currentFrame);
        if (drawing < 0) continue;  // このコマにセルなし
        const bool isActiveCel = (ci == m_activeCel);
        for (size_t li = 0; li < cel.layerCount(); ++li) {
            core::Layer& layer = cel.layer(li);
            if (static_cast<size_t>(drawing) >= layer.frameCount()) continue;
            const core::Bitmap* bitmap = &layer.frame(static_cast<size_t>(drawing)).bitmap();

            // 仕上げ表示中は色トレス線・作監修正レイヤーを隠す(最終画プレビュー)
            const bool hiddenByCleanView =
                m_cleanView && (layer.role() == core::LayerRole::ColorTrace || layer.role() == core::LayerRole::Correction);
            if (layer.visible() && !hiddenByCleanView) {
                stack.push_back(bitmap);
                fillBoundary.push_back(bitmap);
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
    m_canvas->setFillBoundaryLayers(std::move(fillBoundary));
    m_canvas->setLayerStack(std::move(stack), editTarget);
}

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("レイヤー 1");
    core::Frame& frame = layer.addFrame();
    frame.bitmap() = makeTransparentCel();
    cut.setFrameCount(1);
    cel.setExposure(0, 0);  // コマ1に動画1を割付

    m_currentFrame = 0;
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
}

void MainWindow::addFrameAfterCurrent() {
    if (m_playing) return;
    // 新しい動画を作る(セル内全レイヤーに同時追加)。
    // 現在コマが空欄(未割付)ならその場に割り付け、コマは移動しない。
    // 既に割付済みなら次のコマに割り付け、次のコマが尺を超える場合は尺を伸ばす
    core::Cel& cel = activeCel();
    const int newDrawing = static_cast<int>(cel.drawingCount());
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        cel.layer(li).addFrame().bitmap() = makeTransparentCel();
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
    connect(m_xsheetPanel, &XsheetPanel::celVisibilityToggleRequested, this, &MainWindow::toggleCelVisibility);

    updateLayerPanel();
    updatePalettePanel();
    updateXsheetPanel();
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
        layer.addFrame().bitmap() = makeTransparentCel();  // 既存レイヤーとコマ数を揃える
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

void MainWindow::toggleCelVisibility(int celIndex) {
    if (m_playing) return;
    core::Cut& cut = activeCut();
    if (celIndex < 0 || static_cast<size_t>(celIndex) >= cut.celCount()) return;

    core::Cel& cel = cut.cel(static_cast<size_t>(celIndex));
    cel.setVisible(!cel.visible());

    m_dirty = true;
    updateCanvasLayers();
    updateXsheetPanel();
    updateLayerPanel();
    updateFrameLabel();
    updateWindowTitle();
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

    // 編集メニュー
    QMenu* editMenu = menuBar()->addMenu(tr("編集(&E)"));
    QAction* undoAction = editMenu->addAction(tr("元に戻す(&U)"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &MainWindow::undo);
    QAction* redoAction = editMenu->addAction(tr("やり直す(&R)"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::redo);

    // 表示メニュー: 各ドックパネルの表示/非表示(パネル追加時はここに並べる)
    QMenu* viewMenu = menuBar()->addMenu(tr("表示(&V)"));
    viewMenu->addAction(m_framePanel->toggleViewAction());
    viewMenu->addAction(m_layerPanel->toggleViewAction());
    viewMenu->addAction(m_palettePanel->toggleViewAction());
    viewMenu->addAction(m_xsheetPanel->toggleViewAction());
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
    m_activeCel = 0;
    m_activeLayer = 0;
    m_canvas->clearTextureCache();
    setCurrentFrame(0);
    updateLayerPanel();
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
