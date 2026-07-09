#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>

class QListWidget;
class QListWidgetItem;

// レイヤー一覧のドッキングパネル。上のレイヤーがリストの上に並ぶ(リスト行0=最上位レイヤー)。
// 各行のチェックボックスで可視/不可視を切り替えられる。下部のボタン列で追加/削除/上下移動を行う。
class LayerPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit LayerPanel(QWidget* parent = nullptr);

    // レイヤー名・可視状態・アクティブインデックスを反映する(シグナルは発火しない)。
    // 引数は全て下から上(コア側インデックス順)で渡す
    void setLayers(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop, int activeIndex);

signals:
    void layerSelected(int layerIndex);            // コア側インデックス(下から数える)
    void visibilityChanged(int layerIndex, bool visible);  // コア側インデックス
    void addRequested();
    void removeRequested();
    void moveUpRequested();
    void moveDownRequested();
    // レイヤー種別変更要求。layerIndexはコア側インデックス、roleは0=Normal,1=ColorTrace,2=Correction
    void roleChangeRequested(int layerIndex, int role);

private:
    // リスト行(0=最上位)とコア側インデックス(0=最下位)を相互変換する
    int rowToLayerIndex(int row) const;
    int layerIndexToRow(int layerIndex) const;
    void showContextMenu(const QPoint& pos);

    QListWidget* m_list = nullptr;
    bool m_updating = false;
};
