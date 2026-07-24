#include "FloatingCanvasWindow.h"

#include <QCloseEvent>
#include <QTimer>

FloatingCanvasWindow::FloatingCanvasWindow(const QString& title, QWidget* owner)
    : QMainWindow(nullptr, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                              Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                              Qt::WindowCloseButtonHint) {
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1000, 720);

    if (owner) {
        connect(owner, &QObject::destroyed, this, [this] {
            m_restoreQueued = true;
            hide();
            deleteLater();
        });
    }
}

void FloatingCanvasWindow::closeEvent(QCloseEvent* event) {
    if (m_restoreQueued) {
        event->ignore();
        return;
    }

    m_restoreQueued = true;
    event->ignore();
    setEnabled(false);
    QTimer::singleShot(0, this, [this] {
        emit restoreRequested();
        hide();
        deleteLater();
    });
}
