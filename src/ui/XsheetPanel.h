#pragma once

#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>
#include <vector>

class QAction;
class QButtonGroup;
class QLabel;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QWidget;

// 縦読みのタイムシート。左からA/B/Cの順にセルを並べ、行は1始まりのコマを表す。
// 数字が動画の先頭、縦線が同じ動画の継続、空欄が空セル。
class XsheetPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit XsheetPanel(QWidget* parent = nullptr);

    void setSheet(const QStringList& celNames, const QList<bool>& celVisible,
                  const QList<QList<int>>& exposures, const QList<QStringList>& actionTracks,
                  int frameCount, int currentFrame, int activeCel, int fps = 24);
    void startKeyDrawingWorkflow();
    void debugSelectExposureRange(int celIndex, int firstFrame, int lastFrame);
    void debugSelectActionCell(int celIndex, int frame);
    void debugSetActionMarker(const QString& marker) { setActionSelection(marker); }
    void debugSetViewMode(int mode);
    void debugFillHoldSelection() { fillHoldSelection(); }
    bool debugHasPairedColumns() const;

signals:
    void sheetEditsRequested(const QList<int>& exposureCelIndices,
                             const QList<int>& exposureFrames,
                             const QList<int>& drawings,
                             const QList<int>& actionCelIndices,
                             const QList<int>& actionFrames,
                             const QStringList& actionEntries);
    void cellClicked(int celIndex, int frame);
    void frameCountChanged(int frameCount);
    void stepPatternRequested(int celIndex, int step, int startFrame, int endFrame);
    void addDrawingRequested();
    void addKeyDrawingRequested();
    void addInbetweenDrawingRequested();
    void deleteDrawingRequested();
    void celAddRequested();
    void celDuplicateRequested(int celIndex);
    void celRemoveRequested();
    void celRenameRequested();
    void celMoveRequested(int delta);
    void celVisibilityToggleRequested(int celIndex);
    void tableFocusChanged(bool focused);

private:
    enum class WorkStage {
        Key = 0,
        Inbetween = 1,
        Review = 2,
    };

    struct PendingExposureEdit {
        int cel = -1;
        int frame = -1;
        int drawing = -1;
    };

    struct PendingActionEdit {
        int cel = -1;
        int frame = -1;
        QString entry;
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
    void setActionSelection(const QString& entry);
    void promptKeyNumber();
    void setWorkStage(WorkStage stage);
    void emitEdits(const std::vector<PendingExposureEdit>& exposureEdits,
                   const std::vector<PendingActionEdit>& actionEdits = {});
    void updateActionStates();
    void updateEditControls();
    int selectedEditableCount() const;
    int selectedCellCount() const;
    bool selectedFrameRange(int& firstFrame, int& lastFrame) const;
    void updateRowBackgrounds(int frame);
    int colToCel(int col) const;
    bool isActionColumn(int col) const;
    int celToActionCol(int celIndex) const;
    int celToCellCol(int celIndex) const;
    int celToPrimaryCol(int celIndex) const;
    int sheetColumnCount(int celCount) const;
    QString timeLabel(int zeroBasedFrame) const;

    static constexpr int kTimingColumn = 0;

    QTableWidget* m_table = nullptr;
    QSpinBox* m_frameCountSpin = nullptr;
    QLabel* m_currentTimeLabel = nullptr;
    QLabel* m_durationLabel = nullptr;
    QLabel* m_pendingLabel = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_cutAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_holdAction = nullptr;
    QAction* m_addKeyAction = nullptr;
    QAction* m_addInbetweenAction = nullptr;
    QWidget* m_actionControls = nullptr;
    QWidget* m_cellControls = nullptr;
    QToolButton* m_addKeyButton = nullptr;
    QToolButton* m_addInbetweenButton = nullptr;
    QButtonGroup* m_viewModeButtons = nullptr;
    QList<QList<int>> m_exposures;
    QList<QStringList> m_actionTracks;
    QStringList m_celNames;
    QList<bool> m_celVisible;
    int m_frameCount = 1;
    int m_currentFrame = 0;
    int m_activeCel = 0;
    int m_fps = 24;
    WorkStage m_workStage = WorkStage::Key;
    bool m_updating = false;
};
