#pragma once

#include <QMainWindow>
#include <memory>

#include "core/Project.h"

class GLCanvas;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    GLCanvas* canvas() const { return m_canvas; }

private:
    void createNewDocument();
    void setupToolBar();

    std::unique_ptr<core::Project> m_project;
    GLCanvas* m_canvas = nullptr;
};
