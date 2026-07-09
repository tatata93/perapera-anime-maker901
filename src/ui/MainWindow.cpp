#include "MainWindow.h"

#include "render/GLCanvas.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("perapera-anime-maker901");

    m_canvas = new GLCanvas(this);
    setCentralWidget(m_canvas);
}
