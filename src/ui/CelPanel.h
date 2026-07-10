#pragma once

#include <QDockWidget>
#include <QList>
#include <QStringList>

class QListWidget;
class QListWidgetItem;

// セル一覧のドッキングパネル。上のセルがリストの上に並ぶ(リスト行0=最前面セル)。
// 各行のチェックボックスで可視/不可視を切り替えられる。
// 追加/削除/並べ替えはXsheetパネル側の既存ボタン・右クリックメニューが担当するため、
// このパネルはリスト表示とクリック操作のみを持つ(ボタンなし)。
class CelPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit CelPanel(QWidget* parent = nullptr);

    // セル名・可視状態・アクティブインデックスを反映する(シグナルは発火しない)。
    // 引数は全てコア側インデックス順(0=最奥)で渡す
    void setCels(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop, int activeIndex);

signals:
    void celSelected(int celIndex);                  // コア側インデックス
    void visibilityChanged(int celIndex, bool visible);  // コア側インデックス

private:
    // リスト行(0=最前面)とコア側インデックス(0=最奥)を相互変換する
    int rowToCelIndex(int row) const;
    int celIndexToRow(int celIndex) const;

    QListWidget* m_list = nullptr;
    bool m_updating = false;
};
