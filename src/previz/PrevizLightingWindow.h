#pragma once

#include <QDialog>

#include "core/Previz.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QToolButton;
class QWidget;

class PrevizLightingWindow : public QDialog {
    Q_OBJECT

public:
    explicit PrevizLightingWindow(QWidget* parent = nullptr);

    void setScene(core::PrevizScene* scene);
    void setFrame(size_t frame);
    void setAimTarget(core::Vec3 target, bool available);
    void selectLight(int index);
    int selectedLightIndex() const;
    void applyPreset(int presetIndex);
    void debugSetDetailsVisible(bool visible) { setDetailsVisible(visible); }
    bool debugExerciseSimpleControls();

signals:
    void lightingEdited();
    void selectedLightChanged(int index);

private:
    core::PrevizLight* selectedLight();
    core::PrevizLightState& editableState(core::PrevizLight& light);
    void rebuildList();
    void refreshControls();
    void applyControls();
    void chooseLightColor();
    void chooseAmbientColor();
    void addLight(core::PrevizLightType type);
    void duplicateLight();
    void removeLight();
    void aimAtTarget();
    void aimPrimaryAtTarget();
    void applySimpleDirection(int directionIndex);
    void applySimpleIntensity(int percent);
    void applySimpleAmbient(int percent);
    void applySimpleShadowEnabled(bool enabled);
    void applySimpleShadowOpacity(int percent);
    void chooseSimpleLightColor();
    void refreshSimpleControls();
    void setDetailsVisible(bool visible);
    void updateControlAvailability();
    void emitLightingEdited();
    core::PrevizLight* primaryLight();

    core::PrevizScene* m_scene = nullptr;
    size_t m_frame = 0;
    core::Vec3 m_aimTarget;
    bool m_hasAimTarget = false;
    bool m_updating = false;

    QListWidget* m_list = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QDoubleSpinBox* m_ambientIntensity = nullptr;
    QPushButton* m_ambientColorButton = nullptr;
    QComboBox* m_simpleDirectionCombo = nullptr;
    QSlider* m_simpleIntensitySlider = nullptr;
    QLabel* m_simpleIntensityLabel = nullptr;
    QSlider* m_simpleAmbientSlider = nullptr;
    QLabel* m_simpleAmbientLabel = nullptr;
    QPushButton* m_simpleColorButton = nullptr;
    QCheckBox* m_simpleShadowCheck = nullptr;
    QSlider* m_simpleShadowSlider = nullptr;
    QLabel* m_simpleShadowLabel = nullptr;
    QPushButton* m_simpleAimButton = nullptr;
    QWidget* m_simplePanel = nullptr;
    QToolButton* m_detailsButton = nullptr;
    QWidget* m_advancedPanel = nullptr;

    QWidget* m_editor = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QPushButton* m_colorButton = nullptr;
    QDoubleSpinBox* m_intensitySpin = nullptr;
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_targetX = nullptr;
    QDoubleSpinBox* m_targetY = nullptr;
    QDoubleSpinBox* m_targetZ = nullptr;
    QDoubleSpinBox* m_rangeSpin = nullptr;
    QDoubleSpinBox* m_coneSpin = nullptr;
    QCheckBox* m_shadowCheck = nullptr;
    QDoubleSpinBox* m_shadowOpacitySpin = nullptr;
    QDoubleSpinBox* m_shadowSoftnessSpin = nullptr;
    QPushButton* m_aimButton = nullptr;
    QPushButton* m_addKeyButton = nullptr;
    QPushButton* m_removeKeyButton = nullptr;
    QLabel* m_keyStatusLabel = nullptr;
};
