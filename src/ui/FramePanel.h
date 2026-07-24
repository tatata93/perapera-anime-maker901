#pragma once

#include <QDockWidget>
#include <QList>
#include <QSet>

class QListWidget;
class QComboBox;
class QPushButton;

// 動画(絵)一覧のドッキングパネル。クリックで現在コマへの割付を切り替える。
// 並び順(番号順/再生順)を上部のコンボボックスで切り替えられ、下部のボタンで動画の追加/複製/選択中の動画の削除ができる。
// 各項目にはライトテーブル(任意動画の透かし表示)用のチェックボックスも並ぶ。
class FramePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit FramePanel(QWidget* parent = nullptr);

    // 表示順に並んだ動画一覧を反映する(シグナルは発火しない)。
    // displayOrder[表示行] = 動画インデックス(0始まり)。currentDrawingは現在選択中の動画インデックス。
    // ライトテーブルのチェック状態は動画インデックスをキーに保持し、再構築後も引き継ぐ
    void setDrawings(const QList<int>& displayOrder, const QList<int>& drawingKinds,
                     int currentDrawing);

    // 現在の並び順(0=番号順、1=再生順)。MainWindowがdisplayOrderを組み立てる際に参照する
    int sortMode() const;

    // ライトテーブルにチェックが入っている動画インデックスの一覧(昇順)
    QList<int> lightTableDrawings() const;

signals:
    void frameSelected(int drawingIndex);     // 動画インデックス(0始まり)
    void addRequested();                      // 動画追加要求
    void duplicateRequested(int drawingIndex);  // 選択中の動画の複製要求(0始まり、未選択なら発火しない)
    void deleteRequested(int drawingIndex);   // 選択中の動画の削除要求(0始まり、未選択なら発火しない)
    void sortModeChanged();                   // 並び順切替。MainWindowが動画一覧を再構築して反映する
    void lightTableChanged();                 // ライトテーブルのチェック状態変更。引数はlightTableDrawings()で取得する

private:
    QComboBox* m_sortCombo = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_duplicateButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QList<int> m_displayOrder;         // 表示行 → 動画インデックスの対応表
    QSet<int> m_lightTableChecked;      // ライトテーブル表示にチェックされている動画インデックス
    bool m_updating = false;
};
