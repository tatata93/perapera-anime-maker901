#pragma once

#include <QDockWidget>
#include <QList>

class QListWidget;
class QComboBox;
class QPushButton;

// 動画(絵)一覧のドッキングパネル。クリックで現在コマへの割付を切り替える。
// 並び順(番号順/再生順)を上部のコンボボックスで切り替えられ、下部のボタンで選択中の動画を削除できる。
class FramePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit FramePanel(QWidget* parent = nullptr);

    // 表示順に並んだ動画一覧を反映する(シグナルは発火しない)。
    // displayOrder[表示行] = 動画インデックス(0始まり)。currentDrawingは現在選択中の動画インデックス
    void setDrawings(const QList<int>& displayOrder, int currentDrawing);

    // 現在の並び順(0=番号順、1=再生順)。MainWindowがdisplayOrderを組み立てる際に参照する
    int sortMode() const;

signals:
    void frameSelected(int drawingIndex);    // 動画インデックス(0始まり)
    void deleteRequested(int drawingIndex);  // 選択中の動画の削除要求(0始まり、未選択なら発火しない)
    void sortModeChanged();                  // 並び順切替。MainWindowが動画一覧を再構築して反映する

private:
    QComboBox* m_sortCombo = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QList<int> m_displayOrder;  // 表示行 → 動画インデックスの対応表
    bool m_updating = false;
};
