#include "MainWindow.h"

#include <QActionGroup>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <algorithm>

#include "core/BrushEngine.h"
#include "render/GLCanvas.h"

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
constexpr int kDefaultFps = 12;

core::Bitmap makePaperBitmap() {
    core::Bitmap bitmap(kCanvasWidth, kCanvasHeight);
    bitmap.fill({255, 255, 255, 255});
    return bitmap;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("perapera-anime-maker901");

    m_canvas = new GLCanvas(this);
    setCentralWidget(m_canvas);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onPlaybackTick);

    createNewDocument();
    setupToolBar();
    updateFrameLabel();
}

MainWindow::~MainWindow() = default;

core::Layer& MainWindow::activeLayer() {
    // MVPでは1シーン・1カット・1レイヤー固定
    return m_project->scene(0).cut(0).layer(0);
}

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Layer& layer = cut.addLayer("Layer 1");
    core::Frame& frame = layer.addFrame();
    frame.bitmap() = makePaperBitmap();

    m_currentFrame = 0;
    m_canvas->setBitmap(&frame.bitmap());
    updateOnionSkin();
}

void MainWindow::setCurrentFrame(size_t index) {
    core::Layer& layer = activeLayer();
    if (layer.frameCount() == 0) return;
    m_currentFrame = std::min(index, layer.frameCount() - 1);
    m_canvas->setBitmap(&layer.frame(m_currentFrame).bitmap());
    updateOnionSkin();
    updateFrameLabel();
}

void MainWindow::addFrameAfterCurrent() {
    if (m_playing) return;
    core::Layer& layer = activeLayer();
    core::Frame& frame = layer.insertFrame(m_currentFrame + 1);
    frame.bitmap() = makePaperBitmap();
    m_canvas->clearTextureCache();  // 挿入でフレーム構造が変わったため
    setCurrentFrame(m_currentFrame + 1);
}

void MainWindow::deleteCurrentFrame() {
    if (m_playing) return;
    core::Layer& layer = activeLayer();
    if (layer.frameCount() <= 1) return;  // 最後の1枚は消さない
    layer.removeFrame(m_currentFrame);
    m_canvas->clearTextureCache();  // 削除されたBitmapのテクスチャを破棄
    setCurrentFrame(m_currentFrame > 0 ? m_currentFrame - 1 : 0);
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
    if (!m_frameLabel) return;
    m_frameLabel->setText(QStringLiteral(" %1 / %2 ").arg(m_currentFrame + 1).arg(activeLayer().frameCount()));
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
        frame.bitmap() = makePaperBitmap();
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
