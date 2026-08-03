#pragma once

#include <QColor>
#include <QMainWindow>
#include <cstddef>

class QButtonGroup;
class QCloseEvent;
class QDoubleSpinBox;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QShowEvent;
class QSlider;
class QToolButton;

class DrawingFocusPalette : public QMainWindow {
    Q_OBJECT

public:
    explicit DrawingFocusPalette(QWidget* parent = nullptr);

    void setActiveTool(int tool);
    void setNavigationMode(bool enabled);
    void setBrushWidth(double width);
    void setPenColor(const QColor& color);
    void setView(float zoom, qreal rotationDeg);
    void setFrame(size_t currentFrame, size_t frameCount);
    void setUndoRedoEnabled(bool undoEnabled, bool redoEnabled);

signals:
    void returnToNormalRequested();
    void undoRequested();
    void redoRequested();
    void previousFrameRequested();
    void nextFrameRequested();
    void toolRequested(int tool);
    void navigationModeChanged(bool enabled);
    void brushWidthChanged(double width);
    void colorRequested();
    void zoomChanged(double zoomPercent);
    void rotationChanged(double rotationDeg);
    void viewResetRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* addToolButton(const QString& text, int toolId);
    bool handleShortcutKey(QKeyEvent* event);

    QButtonGroup* m_toolGroup = nullptr;
    QToolButton* m_navigationButton = nullptr;
    QSlider* m_widthSlider = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QToolButton* m_colorButton = nullptr;
    QDoubleSpinBox* m_zoomSpin = nullptr;
    QDoubleSpinBox* m_rotationSpin = nullptr;
    QLabel* m_frameLabel = nullptr;
    QToolButton* m_undoButton = nullptr;
    QToolButton* m_redoButton = nullptr;
};
