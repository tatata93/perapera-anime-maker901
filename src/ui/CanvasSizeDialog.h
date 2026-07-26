#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

// プロジェクトの出力基準となるキャンバス解像度とフレームレートを設定する。
// ショートカットはプロジェクト固有ではないため、ShortcutSettingsDialogで別に扱う。
class CanvasSizeDialog : public QDialog {
    Q_OBJECT

public:
    CanvasSizeDialog(int currentW, int currentH, int currentFps, QWidget* parent = nullptr);
    CanvasSizeDialog(int currentW, int currentH, QWidget* parent);

    int canvasWidth() const;
    int canvasHeight() const;
    int fps() const;
    bool resizeExistingArtwork() const;

private:
    void applyPreset(int index);
    void onSizeEdited();
    void swapDimensions();
    void updateSummary();

    int m_originalWidth = 1920;
    int m_originalHeight = 1080;
    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QLabel* m_aspectLabel = nullptr;
    QLabel* m_changeLabel = nullptr;
    QLabel* m_cropWarningLabel = nullptr;
    QCheckBox* m_resizeArtworkCheck = nullptr;
    bool m_updatingFromPreset = false;
};
