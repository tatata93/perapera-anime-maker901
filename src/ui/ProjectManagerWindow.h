#pragma once

#include <QMainWindow>
#include <array>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QLineEdit;
class QHBoxLayout;
class QPushButton;
class QProgressBar;

namespace core {
class Project;
}

// プロジェクト管理ウィンドウ(別ウィンドウ)。プロジェクト全体の制作進行を一覧・集計する
// 「進行管理表」+カット構成(並び替え・追加・削除)の管理を行う。
// 各カットの進捗(7段階)・尺・絵コンテ内容/セリフをその場で編集でき、上部に完成率や
// 工程別の本数を集計表示する。作画そのものはメインウィンドウで行うため、ここでは
// プロジェクト全体の俯瞰と進行管理に専念する(編集(カッティング)ウィンドウの通しプレビューとは別)。
class ProjectManagerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ProjectManagerWindow(QWidget* parent = nullptr);
    ~ProjectManagerWindow() override;

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // 秒換算・総尺集計に使うfps(メインウィンドウのFPS設定を渡す)
    void setFps(int fps);
    // カット一覧と集計を再構築する(m_updatingガードで編集シグナルの暴発を防ぐ)
    void refresh();

signals:
    void edited();                  // カット名/尺/進捗/内容/セリフ、またはカット順序が編集された
    void cutActivated(int index);   // 「選択カットを開く」= そのカットをメインウィンドウでアクティブにする
    void addCutRequested();         // カット追加(セル/レイヤー初期化が要るのでメインウィンドウに委譲)
    void removeCutRequested(int index);  // カット削除(アクティブカットのクランプ等をメインウィンドウが行う)
    void newProjectRequested();     // 新規プロジェクト(作成ダイアログはメインウィンドウが出す)
    void openProjectRequested();    // プロジェクトを開く
    void saveProjectRequested();    // プロジェクトを保存
    void projectSettingsRequested(); // プロジェクト設定を開く

private:
    void rebuildTable();
    void onItemChanged(QTableWidgetItem* item);
    void moveSelectedCut(int delta);
    void activateSelectedCut();
    void updateSummary();
    int selectedRow() const;

    core::Project* m_project = nullptr;
    int m_fps = 24;
    bool m_updating = false;  // refresh中の編集シグナル暴発防止

    QLineEdit* m_projectNameEdit = nullptr;
    QLabel* m_summaryLabel = nullptr;       // 総カット数・総尺・完成率
    QProgressBar* m_doneBar = nullptr;      // 完成率(完成カット数 / 総カット数)
    QHBoxLayout* m_stageBarLayout = nullptr;  // 工程別の割合を積み上げ表示する帯
    std::array<QLabel*, 7> m_stageSegments{};  // 帯の各工程セグメント(stretch=本数)
    QLabel* m_stageLegendLabel = nullptr;   // 工程別本数の凡例テキスト

    QTableWidget* m_table = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
};
