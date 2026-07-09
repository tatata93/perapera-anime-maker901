#pragma once

#include <QColor>
#include <QMainWindow>
#include <QStringList>
#include <memory>

#include "core/CommandStack.h"
#include "core/Project.h"

class FramePanel;
class GLCanvas;
class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

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
    // ファイルI/O確認用(ダイアログを出さない)
    bool debugSaveTo(const QString& path) { return saveToFile(path); }
    bool debugLoadFrom(const QString& path) { return loadFromFile(path); }
    void debugNewDocument() { newDocument(); }
    // Undo/Redo確認用
    void debugUndo() { undo(); }
    void debugRedo() { redo(); }
    // 下敷き確認用: ファイルダイアログを出さずに指定パスから下敷き連番を設定する
    void debugSetUnderlayFile(const QString& path);

private:
    void createNewDocument();
    void setupToolBar();
    void setupMenus();
    void setupPanels();

    void newDocument();
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    void saveAs();
    void save();
    void open();
    void updateWindowTitle();
    void undo();
    void redo();

    core::Layer& activeLayer();
    void setCurrentFrame(size_t index);
    void addFrameAfterCurrent();
    void deleteCurrentFrame();
    void togglePlayback();
    void onPlaybackTick();
    void updateOnionSkin();
    void updateFrameLabel();

    // 下敷き(参照画像/連番シーケンス)。セッション限定でプロジェクトには保存しない
    void openUnderlay();
    void clearUnderlaySequence();
    void setUnderlaySequenceFromFile(const QString& path);
    void updateUnderlay();

    // ブラシ設定UI
    void choosePenColor();
    void updatePenColorButton();

    std::unique_ptr<core::Project> m_project;
    core::CommandStack m_commands;
    GLCanvas* m_canvas = nullptr;

    size_t m_currentFrame = 0;
    bool m_onionEnabled = true;
    bool m_playing = false;

    QTimer* m_playTimer = nullptr;
    QLabel* m_frameLabel = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_onionAction = nullptr;
    FramePanel* m_framePanel = nullptr;
    QString m_currentFilePath;

    // ブラシ設定UI(太さスライダー・色選択ボタン)
    QSlider* m_penRadiusSlider = nullptr;
    QLabel* m_penRadiusValueLabel = nullptr;
    QToolButton* m_penColorButton = nullptr;
    QColor m_penColor = Qt::black;

    // 下敷き(参照画像/連番シーケンス): 選択フォルダ内の同拡張子ファイル一覧(名前順)
    QStringList m_underlaySequence;
    int m_underlayLoadedIndex = -1;  // 直前にロードした連番のインデックス(再ロード抑止用)
};
