#pragma once

#include <QMainWindow>

class QTableWidgetItem;
class QTableWidget;
class QLabel;
class QPushButton;
class GLCanvas;

namespace core {
class Project;
}

// 絵コンテウィンドウ(別ウィンドウ)。絵コンテは全工程の前に単体で描くもの:
// 独立した「パネル(コマ)」の列で、パネルごとにラフ絵を手描きする。
// 同じカット番号を複数パネルに書けば「1カット複数コマ」のコンテになる。
class StoryboardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit StoryboardWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // 全行を再構築し、サムネイルを撮り直す(m_updatingガードで編集シグナルの暴発を防ぐ)。
    // vector再配置に備え、描画エリアも選択パネルへ再設定する
    void refresh();

signals:
    void edited();  // カット番号/内容/セリフ/尺またはコンテ絵が編集された
    // 「パネルからカット作成」。cutLabelは選択パネルのカット番号、totalFramesは
    // 同じカット番号を持つ全パネルのduration合計
    void createCutRequested(QString cutLabel, int totalFrames);

private:
    void onItemChanged(QTableWidgetItem* item);
    void onSelectionChanged();
    void addPanel();
    void removePanel();
    void movePanel(int delta);
    void createCutFromPanel();
    void onStrokeFinished();
    void updateTotalDurationLabel();
    void updateThumbnail(int row);
    // 選択中パネルのdrawingへ描画エリアを再設定する(vectorの再配置でポインタが
    // 無効になるため、パネル追加/削除/並べ替えの後は必ず呼ぶこと)
    void bindCanvasToSelectedPanel();
    int selectedPanelIndex() const;

    core::Project* m_project = nullptr;
    QTableWidget* m_table = nullptr;
    GLCanvas* m_canvas = nullptr;
    QLabel* m_totalLabel = nullptr;
    QPushButton* m_penButton = nullptr;
    QPushButton* m_eraserButton = nullptr;
    bool m_updating = false;
    int m_selectedRow = -1;  // 現在選択中のパネル行(パネルが1枚もなければ-1)
};
