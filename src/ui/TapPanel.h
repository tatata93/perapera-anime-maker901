#pragma once

#include <QDockWidget>
#include <QList>
#include <tuple>

class QListWidget;

// タップ(位置キー)一覧のドッキングパネル。アクティブセルの位置キーをコマ昇順で一覧表示する。
// 下部ボタンで「現在コマにキー」を打つ/選択中の行のキーを削除する。
class TapPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit TapPanel(QWidget* parent = nullptr);

    // アクティブセルの位置キー一覧(コマ, x, y)をコマ昇順で反映する(シグナルは発火しない)。
    // 現在コマにキーがあればその行を選択状態にする
    void setKeys(const QList<std::tuple<int, float, float>>& keys, int currentFrame);

signals:
    void keySelected(int frame);         // 行クリック。該当コマへ移動する用
    void addKeyRequested();              // 「現在コマにキー」ボタン
    void removeKeyRequested(int frame);  // 「キー削除」ボタン(選択行のコマ)

private:
    QListWidget* m_list = nullptr;
    bool m_updating = false;
};
