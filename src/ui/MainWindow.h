#pragma once

#include <QMainWindow>
#include <memory>

#include "core/Project.h"

class GLCanvas;
class QLabel;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    GLCanvas* canvas() const { return m_canvas; }

    // オニオンスキン確認用: 3フレームに縦線を描き、中央フレームを表示する
    void debugSetupOnionDemo();
    // 再生確認用
    void debugTogglePlayback() { togglePlayback(); }

private:
    void createNewDocument();
    void setupToolBar();

    core::Layer& activeLayer();
    void setCurrentFrame(size_t index);
    void addFrameAfterCurrent();
    void deleteCurrentFrame();
    void togglePlayback();
    void onPlaybackTick();
    void updateOnionSkin();
    void updateFrameLabel();

    std::unique_ptr<core::Project> m_project;
    GLCanvas* m_canvas = nullptr;

    size_t m_currentFrame = 0;
    bool m_onionEnabled = true;
    bool m_playing = false;

    QTimer* m_playTimer = nullptr;
    QLabel* m_frameLabel = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_onionAction = nullptr;
};
