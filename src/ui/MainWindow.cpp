#include "MainWindow.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
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
#include "render/GLCanvas.h"
#include "ui/FramePanel.h"
#include "ui/LayerPanel.h"

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
constexpr int kDefaultFps = 12;

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

// カット内の全セル×全レイヤーから現在フレームの絵を集め、下→上の描画順でキャンバスに渡す
void MainWindow::updateCanvasLayers() {
    core::Cut& cut = activeCut();
    std::vector<const core::Bitmap*> stack;
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        core::Cel& cel = cut.cel(ci);
        if (!cel.visible()) continue;
        for (size_t li = 0; li < cel.layerCount(); ++li) {
            core::Layer& layer = cel.layer(li);
            if (!layer.visible()) continue;
            if (m_currentFrame < layer.frameCount()) {
                stack.push_back(&layer.frame(m_currentFrame).bitmap());
            }
        }
    }

    core::Layer& active = activeLayer();
    core::Bitmap* editTarget =
        m_currentFrame < active.frameCount() ? &active.frame(m_currentFrame).bitmap() : nullptr;
    m_canvas->setLayerStack(std::move(stack), editTarget);
}

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Cel& cel = cut.addCel("セル A");
    core::Layer& layer = cel.addLayer("レイヤー 1");
    core::Frame& frame = layer.addFrame();
    frame.bitmap() = makeTransparentCel();

    m_currentFrame = 0;
    m_activeCel = 0;
    m_activeLayer = 0;
    updateCanvasLayers();
    updateOnionSkin();
}

void MainWindow::setCurrentFrame(size_t index) {
    core::Layer& layer = activeLayer();
    if (layer.frameCount() == 0) return;
    m_currentFrame = std::min(index, layer.frameCount() - 1);
    updateCanvasLayers();
    updateOnionSkin();
    updateFrameLabel();
    updateUnderlay();
}

void MainWindow::addFrameAfterCurrent() {
    if (m_playing) return;
    // セル内の全レイヤーに同時にコマを追加し、レイヤー間でコマ数がずれないようにする
    core::Cel& cel = activeCel();
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        core::Layer& layer = cel.layer(li);
        const size_t insertAt = std::min(m_currentFrame + 1, layer.frameCount());
        layer.insertFrame(insertAt).bitmap() = makeTransparentCel();
    }
    m_commands.clear();             // 構造変更のためUndo履歴を破棄
    m_canvas->clearTextureCache();  // 挿入でフレーム構造が変わったため
    setCurrentFrame(m_currentFrame + 1);
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::deleteCurrentFrame() {
    if (m_playing) return;
    core::Cel& cel = activeCel();
    if (activeLayer().frameCount() <= 1) return;  // 最後の1枚は消さない
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        core::Layer& layer = cel.layer(li);
        if (m_currentFrame < layer.frameCount()) layer.removeFrame(m_currentFrame);
    }
    m_commands.clear();             // 削除されたBitmapを参照するコマンドを破棄
    m_canvas->clearTextureCache();  // 削除されたBitmapのテクスチャを破棄
    setCurrentFrame(m_currentFrame > 0 ? m_currentFrame - 1 : 0);
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::togglePlayback() {
    m_playing = !m_playing;
    if (m_playing) {
        m_playAction->setText(tr("停止"));
        m_canvas->setInputEnabled(false);
        updateOnionSkin();  // 再生中はオニオンスキンを消す
        m_playTimer->start(1000 / std::max(1, m_fpsSpin->value()));
    } else {
        m_playTimer->stop();
        m_playAction->setText(tr("再生"));
        m_canvas->setInputEnabled(true);
        updateOnionSkin();
    }
}

void MainWindow::onPlaybackTick() {
    const size_t count = activeLayer().frameCount();
    if (count == 0) return;
    setCurrentFrame((m_currentFrame + 1) % count);
}

void MainWindow::updateOnionSkin() {
    if (!m_onionEnabled || m_playing) {
        m_canvas->setOnionSkin(nullptr, nullptr);
        return;
    }
    core::Layer& layer = activeLayer();
    const core::Bitmap* prev = m_currentFrame > 0 ? &layer.frame(m_currentFrame - 1).bitmap() : nullptr;
    const core::Bitmap* next = m_currentFrame + 1 < layer.frameCount() ? &layer.frame(m_currentFrame + 1).bitmap() : nullptr;
    m_canvas->setOnionSkin(prev, next);
}

void MainWindow::updateFrameLabel() {
    const int count = static_cast<int>(activeLayer().frameCount());
    if (m_frameLabel) {
        m_frameLabel->setText(QStringLiteral(" %1 / %2 ").arg(m_currentFrame + 1).arg(count));
    }
    if (m_framePanel) {
        m_framePanel->setFrames(count, static_cast<int>(m_currentFrame));
    }
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
        if (!m_playing) setCurrentFrame(static_cast<size_t>(index));
    });

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

    updateLayerPanel();
}

void MainWindow::updateLayerPanel() {
    if (!m_layerPanel) return;
    core::Cel& cel = activeCel();
    QStringList names;
    QList<bool> visible;
    for (size_t li = 0; li < cel.layerCount(); ++li) {
        names.append(QString::fromStdString(cel.layer(li).name()));
        visible.append(cel.layer(li).visible());
    }
    m_layerPanel->setLayers(names, visible, static_cast<int>(m_activeLayer));
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
    viewMenu->addSeparator();
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
    m_commands.undo();
    m_canvas->clearTextureCache();  // 変更されたBitmapのテクスチャを再アップロードさせる
}

void MainWindow::redo() {
    if (m_playing || !m_commands.canRedo()) return;
    m_commands.redo();
    m_canvas->clearTextureCache();
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
    connect(penAction, &QAction::triggered, this, [this] { m_canvas->setTool(GLCanvas::Tool::Pen); });

    QAction* eraserAction = toolBar->addAction(tr("消しゴム"));
    eraserAction->setCheckable(true);
    eraserAction->setShortcut(QKeySequence(Qt::Key_E));
    group->addAction(eraserAction);
    connect(eraserAction, &QAction::triggered, this, [this] { m_canvas->setTool(GLCanvas::Tool::Eraser); });

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
        m_canvas->setPenRadius(static_cast<float>(value));
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

    toolBar->addSeparator();

    // --- フレーム操作 ---
    QAction* prevAction = toolBar->addAction(tr("前フレーム"));
    prevAction->setShortcut(QKeySequence(Qt::Key_Comma));
    connect(prevAction, &QAction::triggered, this, [this] {
        if (!m_playing && m_currentFrame > 0) setCurrentFrame(m_currentFrame - 1);
    });

    QAction* nextAction = toolBar->addAction(tr("次フレーム"));
    nextAction->setShortcut(QKeySequence(Qt::Key_Period));
    connect(nextAction, &QAction::triggered, this, [this] {
        if (!m_playing) setCurrentFrame(m_currentFrame + 1);
    });

    QAction* addAction = toolBar->addAction(tr("フレーム追加"));
    addAction->setShortcut(QKeySequence(Qt::Key_A));
    connect(addAction, &QAction::triggered, this, &MainWindow::addFrameAfterCurrent);

    QAction* deleteAction = toolBar->addAction(tr("フレーム削除"));
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

void MainWindow::debugSetLayerVisible(int layerIndex, bool visible) {
    core::Cel& cel = activeCel();
    if (static_cast<size_t>(layerIndex) >= cel.layerCount()) return;
    cel.layer(static_cast<size_t>(layerIndex)).setVisible(visible);
    updateCanvasLayers();
    updateLayerPanel();
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
