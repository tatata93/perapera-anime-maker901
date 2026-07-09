#pragma once

#include <QDockWidget>
#include <QList>

class QColor;
class QListWidget;

// カラーパレットのドッキングパネル。登録色をアイコン一覧(IconMode)で表示する。
// スウォッチをクリックすると colorSelected を発火する。下部ボタンで現在色の追加/選択色の削除を行う。
class PalettePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit PalettePanel(QWidget* parent = nullptr);

    // パレット内容を反映する(シグナルは発火しない)
    void setPalette(const QList<QColor>& colors);

    // 選択中のスウォッチのインデックス(未選択は-1)
    int selectedIndex() const;

signals:
    void colorSelected(QColor color);
    void addCurrentColorRequested();
    void removeSelectedRequested();

private:
    QListWidget* m_list = nullptr;
    bool m_updating = false;
};
