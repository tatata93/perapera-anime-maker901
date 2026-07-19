#include "FloatingCanvasWindow.h"

#include <QCloseEvent>
#include <QTimer>

FloatingCanvasWindow::FloatingCanvasWindow(const QString& title, QWidget* parent) : QMainWindow(parent, Qt::Window) {
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 720);
}

void FloatingCanvasWindow::closeEvent(QCloseEvent* event) {
    if (m_restoreQueued) {
        event->ignore();
        return;
    }

    m_restoreQueued = true;
    event->ignore();
    hide();
    QTimer::singleShot(0, this, [this] {
        emit restoreRequested();
        deleteLater();
    });
}
