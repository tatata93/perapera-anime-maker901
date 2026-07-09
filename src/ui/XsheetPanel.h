#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>

class QTableWidget;
class QTableWidgetItem;
class QSpinBox;

// タイムシート(Xsheet)パネル。行=コマ(行ヘッダ「1」「2」...)、列=セル(列ヘッダ=セル名)。
// 実際のタイムシートの慣習に合わせ、列0(左端)が最前面(セルの内部インデックス末尾)になるよう表示を反転する。
// 各セル欄には動画番号を1始まりで表示・入力する(内部の露出値は0始まり、空欄は-1)。
class XsheetPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit XsheetPanel(QWidget* parent = nullptr);

    // シート内容を反映する(シグナルは発火しない)。
    // exposures[celIndex][frame] = 動画番号(0始まり、-1=空欄)。currentFrame/activeCelの行列を選択表示する。
    // celVisible[celIndex]がfalseのセルは列ヘッダ名の後ろに「 (非表示)」を付け、
    // activeCelの列ヘッダは太字にする
    void setSheet(const QStringList& celNames, const QList<bool>& celVisible, const QList<QList<int>>& exposures,
                  int frameCount, int currentFrame, int activeCel);

signals:
    void exposureEdited(int celIndex, int frame, int drawing);  // drawingは0始まり(空欄/0以下の入力は-1)
    void cellClicked(int celIndex, int frame);
    void frameCountChanged(int frameCount);
    void stepPatternRequested(int step);  // step=1/2/3(コマ打ち)
    void celAddRequested();
    void celRemoveRequested();
    void celRenameRequested();
    void celMoveRequested(int delta);  // -1=前へ(下/奥へ)、+1=後ろへ(上/手前へ)
    void celVisibilityToggleRequested(int celIndex);

private:
    void onItemChanged(QTableWidgetItem* item);
    void onCellClicked(int row, int column);
    void showHeaderContextMenu(const QPoint& pos);

    // 表示列(0=左端)とセル内部インデックス(0=最奥)を相互変換する。
    // タイムシートの慣習(左=最前面)に合わせて反転させる。シグナルは常に内部インデックスを渡す
    int colToCel(int col) const;
    int celToCol(int celIndex) const;

    QTableWidget* m_table = nullptr;
    QSpinBox* m_frameCountSpin = nullptr;
    bool m_updating = false;
};
