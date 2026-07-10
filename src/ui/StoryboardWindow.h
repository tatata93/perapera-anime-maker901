#pragma once

#include <QMainWindow>

class QTableWidgetItem;
class QTableWidget;

namespace core {
class Project;
}

// 絵コンテウィンドウ(別ウィンドウ)。シーン内の全カットを表形式で一覧し、
// サムネイル・カット名・内容(アクション)・セリフ・尺をまとめて確認/編集する。
class StoryboardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit StoryboardWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // 全行を再構築し、サムネイルを撮り直す(m_updatingガードで編集シグナルの暴発を防ぐ)
    void refresh();

signals:
    void edited();          // カット名/内容/セリフが編集された(MainWindowが未保存フラグを立てる)
    void cutRenamed();      // カット名が変更された(カットバーの表示名も更新が必要)
    void cutActivated(int cutIndex);  // サムネ/No列のダブルクリックでそのカットへ切り替える

private:
    void onItemChanged(QTableWidgetItem* item);
    void onCellDoubleClicked(int row, int column);

    core::Project* m_project = nullptr;
    QTableWidget* m_table = nullptr;
    bool m_updating = false;
};
