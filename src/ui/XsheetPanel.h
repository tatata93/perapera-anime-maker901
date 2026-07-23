#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>
#include <vector>

class QAction;
class QLabel;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

// 縦読みのタイムシート。左からA/B/Cの順にセルを並べ、行は1始まりのコマを表す。
// 数字が動画の先頭、縦線が同じ動画の継続、空欄が空セル。
class XsheetPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit XsheetPanel(QWidget* parent = nullptr);

    void setSheet(const QStringList& celNames, const QList<bool>& celVisible, const QList<QList<int>>& exposures,
                  int frameCount, int currentFrame, int activeCel, int fps = 24);
    void debugSelectExposureRange(int celIndex, int firstFrame, int lastFrame);
    void debugFillHoldSelection() { fillHoldSelection(); }

signals:
    void exposureEditsRequested(const QList<int>& celIndices, const QList<int>& frames,
                                const QList<int>& drawings);
    void cellClicked(int celIndex, int frame);
    void frameCountChanged(int frameCount);
    void stepPatternRequested(int step, int startFrame, int endFrame);
    void addDrawingRequested();
    void deleteDrawingRequested();
    void celAddRequested();
    void celRemoveRequested();
    void celRenameRequested();
    void celMoveRequested(int delta);
    void celVisibilityToggleRequested(int celIndex);
    void tableFocusChanged(bool focused);

private:
    struct PendingEdit {
        int cel = -1;
        int frame = -1;
        int drawing = -1;
    };

    void onItemChanged(QTableWidgetItem* item);
    void onCellClicked(int row, int column);
    void showHeaderContextMenu(const QPoint& pos);
    void showCellContextMenu(const QPoint& pos);
    void copySelection();
    void cutSelection();
    void pasteSelection();
    void clearSelection();
    void fillHoldSelection();
    void requestStepPattern(int step);
    void emitEdits(const std::vector<PendingEdit>& edits);
    void updateActionStates();
    int selectedExposureCount() const;
    bool selectedFrameRange(int& firstFrame, int& lastFrame) const;
    void updateRowBackgrounds(int frame);
    int colToCel(int col) const;
    int celToCol(int celIndex) const;
    QString timeLabel(int zeroBasedFrame) const;

    static constexpr int kTimingColumn = 0;

    QTableWidget* m_table = nullptr;
    QSpinBox* m_frameCountSpin = nullptr;
    QLabel* m_currentTimeLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_holdAction = nullptr;
    QList<QList<int>> m_exposures;
    QStringList m_celNames;
    QList<bool> m_celVisible;
    int m_frameCount = 1;
    int m_currentFrame = 0;
    int m_activeCel = 0;
    int m_fps = 24;
    bool m_updating = false;
};
