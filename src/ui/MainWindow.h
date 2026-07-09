#pragma once

#include <QMainWindow>

class GLCanvas;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    GLCanvas* m_canvas = nullptr;
};
