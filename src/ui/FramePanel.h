#pragma once

#include <QDockWidget>

class QListWidget;

// フレーム一覧のドッキングパネル。クリックでフレームを切り替える。
// 将来のタイムシート(Xsheet)パネルの前身となる、パネル拡張の土台。
class FramePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit FramePanel(QWidget* parent = nullptr);

    // フレーム数と選択中インデックスを反映する(シグナルは発火しない)
    void setFrames(int count, int currentIndex);

signals:
    void frameSelected(int index);

private:
    QListWidget* m_list = nullptr;
    bool m_updating = false;
};
