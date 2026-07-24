#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

class FloatingCanvasWindow : public QMainWindow {
    Q_OBJECT

public:
    // owner is used only to track lifetime. The native window intentionally has
    // no QWidget parent so Windows gives it its own taskbar entry and preview.
    explicit FloatingCanvasWindow(const QString& title, QWidget* owner = nullptr);

signals:
    void restoreRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    bool m_restoreQueued = false;
};
