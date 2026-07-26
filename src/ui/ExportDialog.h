#pragma once

#include "ExportSettings.h"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(const QStringList& celNames, int frameCount, int canvasWidth = 1920, int canvasHeight = 1080,
                 int currentFrame = 0, QWidget* parent = nullptr);

    void setOutputPath(const QString& path);
    perapera::ui::ExportSettings settings() const;

protected:
    void accept() override;

private:
    enum class ResolutionPreset {
        Canvas,
        Half,
        Double,
        Hd,
        FullHd,
        Uhd,
        Custom,
    };

    void applyPreset(int presetIndex);
    void markPresetCustom();
    void updateFormatDependentUi();
    void updateResolutionUi();
    void rebuildProfileOptions();
    void browseOutputPath();
    perapera::ui::ExportFormat format() const;
    perapera::ui::ExportScope scope() const;
    perapera::ui::ExportContent content() const;
    int currentProResProfile() const;

    int m_frameCount = 1;
    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;
    int m_currentFrame = 0;
    bool m_applyingPreset = false;
    int m_lastQualityFormat = -1;

    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_formatCombo = nullptr;
    QComboBox* m_contentCombo = nullptr;
    QComboBox* m_scopeCombo = nullptr;
    QLineEdit* m_outputPathEdit = nullptr;
    QSpinBox* m_fromSpin = nullptr;
    QSpinBox* m_toSpin = nullptr;
    QComboBox* m_celCombo = nullptr;
    QCheckBox* m_colorTraceCheck = nullptr;
    QCheckBox* m_correctionCheck = nullptr;
    QCheckBox* m_transparentCheck = nullptr;

    QComboBox* m_resolutionCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QCheckBox* m_preserveAspectCheck = nullptr;
    QLabel* m_qualityLabel = nullptr;
    QSpinBox* m_qualitySpin = nullptr;
    QLabel* m_profileLabel = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QLabel* m_encodeSpeedLabel = nullptr;
    QComboBox* m_encodeSpeedCombo = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QDoubleSpinBox* m_fpsSpin = nullptr;
    QComboBox* m_playbackSpeedCombo = nullptr;
    QLabel* m_sequencePrefixLabel = nullptr;
    QLineEdit* m_sequencePrefixEdit = nullptr;
    QLabel* m_sequenceStartLabel = nullptr;
    QSpinBox* m_sequenceStartSpin = nullptr;
    QLabel* m_sequencePaddingLabel = nullptr;
    QSpinBox* m_sequencePaddingSpin = nullptr;

    QDialogButtonBox* m_buttonBox = nullptr;
};
