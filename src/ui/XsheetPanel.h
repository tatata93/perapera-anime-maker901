#pragma once

#include <QDockWidget>
#include <QList>
#include <QStringList>

class QTableWidget;
class QTableWidgetItem;
class QSpinBox;

// タイムシート(Xsheet)パネル。行=コマ(行ヘッダ「1」「2」...)、列=セル(列ヘッダ=セル名)。
// 各セル欄には動画番号を1始まりで表示・入力する(内部の露出値は0始まり、空欄は-1)。
class XsheetPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit XsheetPanel(QWidget* parent = nullptr);

    // シート内容を反映する(シグナルは発火しない)。
    // exposures[celIndex][frame] = 動画番号(0始まり、-1=空欄)。currentFrame/activeCelの行列を選択表示する
    void setSheet(const QStringList& celNames, const QList<QList<int>>& exposures, int frameCount, int currentFrame,
                  int activeCel);

signals:
    void exposureEdited(int celIndex, int frame, int drawing);  // drawingは0始まり(空欄/0以下の入力は-1)
    void cellClicked(int celIndex, int frame);
    void frameCountChanged(int frameCount);
    void stepPatternRequested(int step);  // step=1/2/3(コマ打ち)

private:
    void onItemChanged(QTableWidgetItem* item);
    void onCellClicked(int row, int column);

    QTableWidget* m_table = nullptr;
    QSpinBox* m_frameCountSpin = nullptr;
    bool m_updating = false;
};
