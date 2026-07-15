#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QSlider;

class CelPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit CelPanel(QWidget* parent = nullptr);

    void setCels(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop,
                 const QList<int>& opacityPercentsBottomToTop, int activeIndex);

signals:
    void celSelected(int celIndex);
    void visibilityChanged(int celIndex, bool visible);
    void opacityChanged(int celIndex, int opacityPercent);
    void addRequested();
    void duplicateRequested(int celIndex);
    void removeRequested();
    void moveUpRequested();
    void moveDownRequested();
    void celSizeRequested();

private:
    int rowToCelIndex(int row) const;
    int celIndexToRow(int celIndex) const;
    void showContextMenu(const QPoint& pos);
    void updateOpacityLabel(int percent);

    QListWidget* m_list = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QSlider* m_opacitySlider = nullptr;
    int m_activeCelIndex = -1;
    bool m_updating = false;
};
