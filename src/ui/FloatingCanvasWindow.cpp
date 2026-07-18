#include "FloatingCanvasWindow.h"

#include <QCloseEvent>

FloatingCanvasWindow::FloatingCanvasWindow(const QString& title, QWidget* parent) : QMainWindow(parent, Qt::Window) {
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 720);
}

void FloatingCanvasWindow::closeEvent(QCloseEvent* event) {
    emit restoreRequested();
    QMainWindow::closeEvent(event);
}
