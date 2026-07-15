#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

class FloatingCanvasWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit FloatingCanvasWindow(const QString& title, QWidget* parent = nullptr);

signals:
    void restoreRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
};
