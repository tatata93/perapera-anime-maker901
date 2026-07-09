#include "MainWindow.h"

#include <QActionGroup>
#include <QToolBar>

#include "render/GLCanvas.h"

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("perapera-anime-maker901");

    m_canvas = new GLCanvas(this);
    setCentralWidget(m_canvas);

    createNewDocument();
    setupToolBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::createNewDocument() {
    m_project = std::make_unique<core::Project>("Untitled");
    core::Scene& scene = m_project->addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Layer& layer = cut.addLayer("Layer 1");
    core::Frame& frame = layer.addFrame();

    frame.bitmap() = core::Bitmap(kCanvasWidth, kCanvasHeight);
    frame.bitmap().fill({255, 255, 255, 255});  // 紙(白)

    m_canvas->setBitmap(&frame.bitmap());
}

void MainWindow::setupToolBar() {
    QToolBar* toolBar = addToolBar(tr("Tools"));
    toolBar->setMovable(false);

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
}
