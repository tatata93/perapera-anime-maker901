#pragma once

#include <QDockWidget>
#include <QList>
#include <QStringList>

class QTableWidget;

// プリビズ用タイムシート。行=コマ(行ヘッダ1始まり)、列=「カメラ」+各モデル名。
// キーがあるコマのセルには「●」を表示する(編集不可)。
class PrevizSheetPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit PrevizSheetPanel(QWidget* parent = nullptr);

    // シート内容を反映する(シグナルは発火しない)。
    // columnNames[0]="カメラ"、以降は各モデル名。keyFlags[列][コマ]=そのコマにキーがあるか。
    // currentFrameの行、activeColumnの列を選択表示する
    void setSheet(const QStringList& columnNames, const QList<QList<bool>>& keyFlags, int frameCount,
                  int currentFrame, int activeColumn);

signals:
    void cellClicked(int column, int frame);
    void keyToggleRequested(int column, int frame);  // ダブルクリックでキーの有無をトグル

private:
    void onCellClicked(int row, int column);
    void onCellDoubleClicked(int row, int column);

    QTableWidget* m_table = nullptr;
    bool m_updating = false;
};
