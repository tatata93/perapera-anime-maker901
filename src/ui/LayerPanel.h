#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QSlider;

// レイヤー一覧のドッキングパネル。リスト行0=最前面レイヤー。
// 選択中レイヤーの基本操作を見える場所にまとめ、右クリックには補助操作を置く。
class LayerPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit LayerPanel(QWidget* parent = nullptr);

    // 引数はコア側インデックス順(0=最奥)で渡す。
    void setLayers(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop,
                   const QList<int>& opacityPercentsBottomToTop, int activeIndex);

signals:
    void layerSelected(int layerIndex);
    void visibilityChanged(int layerIndex, bool visible);
    void opacityChanged(int layerIndex, int opacityPercent);
    void addRequested();
    void duplicateRequested(int layerIndex);
    void removeRequested();
    void moveUpRequested();
    void moveDownRequested();
    void renameRequested(int layerIndex);
    // role: 0=Normal, 1=ColorTrace, 2=Correction
    void roleChangeRequested(int layerIndex, int role);

private:
    int rowToLayerIndex(int row) const;
    int layerIndexToRow(int layerIndex) const;
    void showContextMenu(const QPoint& pos);
    void updateOpacityLabel(int percent);

    QListWidget* m_list = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QSlider* m_opacitySlider = nullptr;
    int m_activeLayerIndex = -1;
    bool m_updating = false;
};
