#include "PrevizLightingWindow.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kMaxRealtimeLights = 4;

QColor colorFromVec(const core::Vec3& color) {
    return QColor::fromRgbF(std::clamp(color.x, 0.0f, 1.0f),
                            std::clamp(color.y, 0.0f, 1.0f),
                            std::clamp(color.z, 0.0f, 1.0f));
}

core::Vec3 vecFromColor(const QColor& color) {
    return {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF())};
}

void setColorButton(QPushButton* button, const core::Vec3& color) {
    if (!button) return;
    const QColor qColor = colorFromVec(color);
    const int luminance = qGray(qColor.rgb());
    button->setStyleSheet(
        QStringLiteral("background:%1; color:%2;").arg(qColor.name(),
                                                       luminance < 128 ? QStringLiteral("white")
                                                                       : QStringLiteral("black")));
}

core::Vec3 normalized(core::Vec3 value, core::Vec3 fallback = {0.0f, -1.0f, 0.0f}) {
    const float length =
        std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 1e-5f) return fallback;
    return {value.x / length, value.y / length, value.z / length};
}

core::PrevizLight makeLight(const std::string& name, core::PrevizLightType type,
                            core::Vec3 color, float intensity, core::Vec3 position,
                            core::Vec3 target, bool shadow) {
    core::PrevizLight light;
    light.name = name;
    light.state.type = type;
    light.state.color = color;
    light.state.intensity = intensity;
    light.state.position = position;
    light.state.direction =
        normalized({target.x - position.x, target.y - position.y, target.z - position.z});
    light.state.castsShadow = shadow;
    return light;
}

QDoubleSpinBox* makeSpin(QWidget* parent, double minimum, double maximum, double step,
                         int decimals = 2) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    return spin;
}

QWidget* makeTripleRow(QWidget* parent, QDoubleSpinBox*& x, QDoubleSpinBox*& y,
                       QDoubleSpinBox*& z, double minimum, double maximum) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    x = makeSpin(row, minimum, maximum, 0.1);
    y = makeSpin(row, minimum, maximum, 0.1);
    z = makeSpin(row, minimum, maximum, 0.1);
    x->setPrefix(QStringLiteral("X "));
    y->setPrefix(QStringLiteral("Y "));
    z->setPrefix(QStringLiteral("Z "));
    layout->addWidget(x);
    layout->addWidget(y);
    layout->addWidget(z);
    return row;
}

QWidget* makeSliderRow(QWidget* parent, QSlider*& slider, QLabel*& valueLabel,
                       int minimum = 0, int maximum = 100) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(minimum, maximum);
    slider->setPageStep(10);
    valueLabel = new QLabel(row);
    valueLabel->setMinimumWidth(42);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);
    return row;
}

float simpleIntensityReference(core::PrevizLightType type) {
    return type == core::PrevizLightType::Directional ? 1.5f : 4.0f;
}

}  // namespace

PrevizLightingWindow::PrevizLightingWindow(QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setWindowTitle(tr("プリビズ照明"));
    resize(540, 320);
    setMinimumSize(500, 300);

    auto* root = new QVBoxLayout(this);

    auto* simpleGroup = new QGroupBox(tr("かんたん照明"), this);
    m_simplePanel = simpleGroup;
    auto* simpleForm = new QFormLayout(simpleGroup);
    m_presetCombo = new QComboBox(simpleGroup);
    m_presetCombo->addItems(
        {tr("標準（3点照明）"), tr("昼・屋外"), tr("夜・室内")});
    simpleForm->addRow(tr("雰囲気"), m_presetCombo);

    m_simpleDirectionCombo = new QComboBox(simpleGroup);
    m_simpleDirectionCombo->addItems(
        {tr("左前から"), tr("正面から"), tr("右前から"), tr("後ろから"),
         tr("真上から")});
    simpleForm->addRow(tr("光の向き"), m_simpleDirectionCombo);

    simpleForm->addRow(
        tr("光の強さ"),
        makeSliderRow(simpleGroup, m_simpleIntensitySlider,
                      m_simpleIntensityLabel, 0, 200));
    simpleForm->addRow(
        tr("暗い部分"),
        makeSliderRow(simpleGroup, m_simpleAmbientSlider, m_simpleAmbientLabel));

    m_simpleColorButton = new QPushButton(tr("光の色を選ぶ"), simpleGroup);
    simpleForm->addRow(tr("光の色"), m_simpleColorButton);

    auto* simpleShadowRow = new QWidget(simpleGroup);
    auto* simpleShadowLayout = new QHBoxLayout(simpleShadowRow);
    simpleShadowLayout->setContentsMargins(0, 0, 0, 0);
    m_simpleShadowCheck = new QCheckBox(tr("つける"), simpleShadowRow);
    simpleShadowLayout->addWidget(m_simpleShadowCheck);
    simpleShadowLayout->addWidget(
        makeSliderRow(simpleShadowRow, m_simpleShadowSlider,
                      m_simpleShadowLabel),
        1);
    simpleForm->addRow(tr("影"), simpleShadowRow);

    m_simpleAimButton =
        new QPushButton(tr("選択モデルを照らす"), simpleGroup);
    simpleForm->addRow(QString(), m_simpleAimButton);
    simpleGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    root->addWidget(simpleGroup);

    m_detailsButton = new QToolButton(this);
    m_detailsButton->setText(tr("詳細設定..."));
    m_detailsButton->setCheckable(true);
    m_detailsButton->setArrowType(Qt::RightArrow);
    m_detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    root->addWidget(m_detailsButton, 0, Qt::AlignLeft);

    m_advancedPanel = new QWidget(this);
    auto* advancedLayout = new QVBoxLayout(m_advancedPanel);
    advancedLayout->setContentsMargins(0, 0, 0, 0);

    auto* ambientGroup = new QGroupBox(tr("環境光の詳細"), m_advancedPanel);
    auto* ambientLayout = new QHBoxLayout(ambientGroup);
    m_ambientIntensity = makeSpin(ambientGroup, 0.0, 2.0, 0.05);
    m_ambientColorButton = new QPushButton(tr("環境色"), ambientGroup);
    ambientLayout->addWidget(new QLabel(tr("明るさ"), ambientGroup));
    ambientLayout->addWidget(m_ambientIntensity, 1);
    ambientLayout->addWidget(m_ambientColorButton);
    advancedLayout->addWidget(ambientGroup);

    auto* body = new QHBoxLayout();
    auto* listColumn = new QVBoxLayout();
    m_list = new QListWidget(m_advancedPanel);
    m_list->setMinimumWidth(190);
    listColumn->addWidget(m_list, 1);

    auto* listButtons = new QHBoxLayout();
    auto* addButton = new QToolButton(m_advancedPanel);
    addButton->setText(tr("追加"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addButton);
    QAction* addDirectional = addMenu->addAction(tr("平行光"));
    QAction* addPoint = addMenu->addAction(tr("点光源"));
    QAction* addSpot = addMenu->addAction(tr("スポット光"));
    addButton->setMenu(addMenu);
    auto* duplicateButton = new QPushButton(tr("複製"), m_advancedPanel);
    auto* removeButton = new QPushButton(tr("削除"), m_advancedPanel);
    listButtons->addWidget(addButton);
    listButtons->addWidget(duplicateButton);
    listButtons->addWidget(removeButton);
    listColumn->addLayout(listButtons);
    body->addLayout(listColumn);

    m_editor = new QGroupBox(tr("選択した光"), m_advancedPanel);
    auto* form = new QFormLayout(m_editor);
    m_nameEdit = new QLineEdit(m_editor);
    form->addRow(tr("名前"), m_nameEdit);
    m_enabledCheck = new QCheckBox(tr("この光を使う"), m_editor);
    form->addRow(QString(), m_enabledCheck);
    m_typeCombo = new QComboBox(m_editor);
    m_typeCombo->addItems({tr("平行光（太陽）"), tr("点光源（電球）"),
                           tr("スポット光")});
    form->addRow(tr("種類"), m_typeCombo);
    m_colorButton = new QPushButton(tr("色を選ぶ"), m_editor);
    form->addRow(tr("色"), m_colorButton);
    m_intensitySpin = makeSpin(m_editor, 0.0, 20.0, 0.1);
    form->addRow(tr("強さ"), m_intensitySpin);
    form->addRow(tr("位置（m）"),
                 makeTripleRow(m_editor, m_posX, m_posY, m_posZ, -1000.0, 1000.0));
    form->addRow(tr("照らす位置（m）"),
                 makeTripleRow(m_editor, m_targetX, m_targetY, m_targetZ, -1000.0,
                               1000.0));
    m_aimButton = new QPushButton(tr("選択モデルへ向ける"), m_editor);
    form->addRow(QString(), m_aimButton);
    m_rangeSpin = makeSpin(m_editor, 0.1, 10000.0, 0.5);
    m_rangeSpin->setSuffix(tr(" m"));
    form->addRow(tr("届く距離"), m_rangeSpin);
    m_coneSpin = makeSpin(m_editor, 2.0, 175.0, 1.0, 1);
    m_coneSpin->setSuffix(tr("°"));
    form->addRow(tr("照射角"), m_coneSpin);
    m_shadowCheck = new QCheckBox(tr("床に影を落とす"), m_editor);
    form->addRow(QString(), m_shadowCheck);
    m_shadowOpacitySpin = makeSpin(m_editor, 0.0, 1.0, 0.05);
    form->addRow(tr("影の濃さ"), m_shadowOpacitySpin);
    m_shadowSoftnessSpin = makeSpin(m_editor, 0.0, 1.0, 0.05);
    form->addRow(tr("影の柔らかさ"), m_shadowSoftnessSpin);

    auto* keyRow = new QWidget(m_editor);
    auto* keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    m_addKeyButton = new QPushButton(tr("現在コマにキー"), keyRow);
    m_removeKeyButton = new QPushButton(tr("キー削除"), keyRow);
    m_keyStatusLabel = new QLabel(keyRow);
    keyLayout->addWidget(m_addKeyButton);
    keyLayout->addWidget(m_removeKeyButton);
    keyLayout->addWidget(m_keyStatusLabel, 1);
    form->addRow(tr("アニメーション"), keyRow);
    body->addWidget(m_editor, 1);
    advancedLayout->addLayout(body, 1);
    root->addWidget(m_advancedPanel, 1);
    m_advancedPanel->hide();

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    closeButtons->button(QDialogButtonBox::Close)->setText(tr("閉じる"));
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    root->addWidget(closeButtons);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::activated), this,
            &PrevizLightingWindow::applyPreset);
    connect(m_simpleDirectionCombo,
            QOverload<int>::of(&QComboBox::activated), this,
            &PrevizLightingWindow::applySimpleDirection);
    connect(m_simpleIntensitySlider, &QSlider::valueChanged, this,
            &PrevizLightingWindow::applySimpleIntensity);
    connect(m_simpleAmbientSlider, &QSlider::valueChanged, this,
            &PrevizLightingWindow::applySimpleAmbient);
    connect(m_simpleColorButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::chooseSimpleLightColor);
    connect(m_simpleShadowCheck, &QCheckBox::toggled, this,
            &PrevizLightingWindow::applySimpleShadowEnabled);
    connect(m_simpleShadowSlider, &QSlider::valueChanged, this,
            &PrevizLightingWindow::applySimpleShadowOpacity);
    connect(m_simpleAimButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::aimPrimaryAtTarget);
    connect(m_detailsButton, &QToolButton::toggled, this,
            &PrevizLightingWindow::setDetailsVisible);
    connect(addDirectional, &QAction::triggered, this,
            [this] { addLight(core::PrevizLightType::Directional); });
    connect(addPoint, &QAction::triggered, this,
            [this] { addLight(core::PrevizLightType::Point); });
    connect(addSpot, &QAction::triggered, this,
            [this] { addLight(core::PrevizLightType::Spot); });
    connect(duplicateButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::duplicateLight);
    connect(removeButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::removeLight);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        refreshControls();
        emit selectedLightChanged(row);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating || !item || !m_scene) return;
        const int row = m_list->row(item);
        if (row < 0 || row >= static_cast<int>(m_scene->lights.size())) return;
        core::PrevizLight& light = m_scene->lights[static_cast<size_t>(row)];
        editableState(light).enabled = item->checkState() == Qt::Checked;
        emitLightingEdited();
    });
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        core::PrevizLight* light = selectedLight();
        if (!light) return;
        light->name = m_nameEdit->text().trimmed().isEmpty()
                          ? tr("ライト").toStdString()
                          : m_nameEdit->text().trimmed().toStdString();
        rebuildList();
        emitLightingEdited();
    });
    connect(m_colorButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::chooseLightColor);
    connect(m_ambientColorButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::chooseAmbientColor);
    connect(m_aimButton, &QPushButton::clicked, this,
            &PrevizLightingWindow::aimAtTarget);

    const auto connectSpin = [this](QDoubleSpinBox* spin) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double) { applyControls(); });
    };
    for (QDoubleSpinBox* spin :
         {m_intensitySpin, m_posX, m_posY, m_posZ, m_targetX, m_targetY,
          m_targetZ, m_rangeSpin, m_coneSpin, m_shadowOpacitySpin,
          m_shadowSoftnessSpin}) {
        connectSpin(spin);
    }
    connect(m_ambientIntensity, &QDoubleSpinBox::valueChanged, this,
            [this](double value) {
                if (m_updating || !m_scene) return;
                m_scene->ambientIntensity =
                    std::clamp(static_cast<float>(value), 0.0f, 2.0f);
                emitLightingEdited();
            });
    connect(m_enabledCheck, &QCheckBox::toggled, this,
            [this](bool) { applyControls(); });
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                applyControls();
                updateControlAvailability();
            });
    connect(m_shadowCheck, &QCheckBox::toggled, this, [this](bool) {
        applyControls();
        updateControlAvailability();
    });
    connect(m_addKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizLight* light = selectedLight();
        if (!light) return;
        light->keys[m_frame] = light->stateAt(m_frame);
        refreshControls();
        emitLightingEdited();
    });
    connect(m_removeKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizLight* light = selectedLight();
        if (!light) return;
        light->keys.erase(m_frame);
        refreshControls();
        emitLightingEdited();
    });

    refreshControls();
}

void PrevizLightingWindow::setScene(core::PrevizScene* scene) {
    m_scene = scene;
    rebuildList();
    refreshControls();
}

void PrevizLightingWindow::setFrame(size_t frame) {
    if (m_frame == frame) return;
    m_frame = frame;
    refreshControls();
}

void PrevizLightingWindow::setAimTarget(core::Vec3 target, bool available) {
    m_aimTarget = target;
    m_hasAimTarget = available;
    if (m_aimButton) m_aimButton->setEnabled(available && selectedLight());
    if (m_simpleAimButton) {
        m_simpleAimButton->setEnabled(available && primaryLight());
    }
}

void PrevizLightingWindow::selectLight(int index) {
    if (!m_list) return;
    m_list->setCurrentRow(index);
    setDetailsVisible(true);
}

int PrevizLightingWindow::selectedLightIndex() const {
    return m_list ? m_list->currentRow() : -1;
}

core::PrevizLight* PrevizLightingWindow::selectedLight() {
    if (!m_scene) return nullptr;
    const int row = selectedLightIndex();
    if (row < 0 || row >= static_cast<int>(m_scene->lights.size())) return nullptr;
    return &m_scene->lights[static_cast<size_t>(row)];
}

core::PrevizLight* PrevizLightingWindow::primaryLight() {
    if (!m_scene || m_scene->lights.empty()) return nullptr;
    return &m_scene->lights.front();
}

core::PrevizLightState& PrevizLightingWindow::editableState(
    core::PrevizLight& light) {
    if (light.keys.empty()) return light.state;
    light.keys[m_frame] = light.stateAt(m_frame);
    return light.keys[m_frame];
}

void PrevizLightingWindow::rebuildList() {
    const int previous = selectedLightIndex();
    m_updating = true;
    m_list->clear();
    if (m_scene) {
        for (const core::PrevizLight& light : m_scene->lights) {
            const core::PrevizLightState state = light.stateAt(m_frame);
            auto* item = new QListWidgetItem(QString::fromStdString(light.name));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(state.enabled ? Qt::Checked : Qt::Unchecked);
            m_list->addItem(item);
        }
    }
    m_updating = false;
    if (m_list->count() > 0) {
        m_list->setCurrentRow(
            std::clamp(previous < 0 ? 0 : previous, 0, m_list->count() - 1));
    }
}

void PrevizLightingWindow::refreshControls() {
    m_updating = true;
    if (m_scene) {
        m_ambientIntensity->setValue(m_scene->ambientIntensity);
        setColorButton(m_ambientColorButton, m_scene->ambientColor);
    }
    core::PrevizLight* light = selectedLight();
    m_editor->setEnabled(light != nullptr);
    if (light) {
        const core::PrevizLightState state = light->stateAt(m_frame);
        m_nameEdit->setText(QString::fromStdString(light->name));
        m_enabledCheck->setChecked(state.enabled);
        m_typeCombo->setCurrentIndex(static_cast<int>(state.type));
        setColorButton(m_colorButton, state.color);
        m_intensitySpin->setValue(state.intensity);
        m_posX->setValue(state.position.x);
        m_posY->setValue(state.position.y);
        m_posZ->setValue(state.position.z);
        const core::Vec3 direction = normalized(state.direction);
        m_targetX->setValue(state.position.x + direction.x * 3.0f);
        m_targetY->setValue(state.position.y + direction.y * 3.0f);
        m_targetZ->setValue(state.position.z + direction.z * 3.0f);
        m_rangeSpin->setValue(state.rangeMeters);
        m_coneSpin->setValue(state.coneAngleDeg);
        m_shadowCheck->setChecked(state.castsShadow);
        m_shadowOpacitySpin->setValue(state.shadowOpacity);
        m_shadowSoftnessSpin->setValue(state.shadowSoftness);
        const bool hasKey = light->keys.count(m_frame) > 0;
        m_keyStatusLabel->setText(hasKey ? tr("このコマにキーあり")
                                         : tr("基本値／補間値"));
        m_removeKeyButton->setEnabled(hasKey);
    }
    refreshSimpleControls();
    m_updating = false;
    updateControlAvailability();
}

void PrevizLightingWindow::refreshSimpleControls() {
    core::PrevizLight* light = primaryLight();
    const bool hasLight = light != nullptr;
    for (QWidget* widget :
         {static_cast<QWidget*>(m_simpleDirectionCombo),
          static_cast<QWidget*>(m_simpleIntensitySlider),
          static_cast<QWidget*>(m_simpleColorButton),
          static_cast<QWidget*>(m_simpleShadowCheck)}) {
        if (widget) widget->setEnabled(hasLight);
    }
    if (m_simpleAimButton) {
        m_simpleAimButton->setEnabled(hasLight && m_hasAimTarget);
    }
    if (m_simpleAmbientSlider) {
        const int ambientPercent =
            m_scene ? std::clamp(static_cast<int>(
                                     std::lround(m_scene->ambientIntensity * 200.0f)),
                                 0, 100)
                    : 0;
        m_simpleAmbientSlider->setValue(ambientPercent);
        m_simpleAmbientLabel->setText(tr("%1%").arg(ambientPercent));
        m_simpleAmbientSlider->setEnabled(m_scene != nullptr);
    }
    if (!hasLight) {
        if (m_simpleIntensityLabel) m_simpleIntensityLabel->setText(tr("--"));
        if (m_simpleShadowLabel) m_simpleShadowLabel->setText(tr("--"));
        if (m_simpleShadowSlider) m_simpleShadowSlider->setEnabled(false);
        return;
    }

    const core::PrevizLightState state = light->stateAt(m_frame);
    const float reference = simpleIntensityReference(state.type);
    const int intensityPercent = std::clamp(
        static_cast<int>(std::lround(state.intensity / reference * 100.0f)),
        0, 200);
    m_simpleIntensitySlider->setValue(intensityPercent);
    m_simpleIntensityLabel->setText(tr("%1%").arg(intensityPercent));
    setColorButton(m_simpleColorButton, state.color);

    m_simpleShadowCheck->setChecked(state.castsShadow);
    const int shadowPercent = std::clamp(
        static_cast<int>(std::lround(state.shadowOpacity * 100.0f)), 0, 100);
    m_simpleShadowSlider->setValue(shadowPercent);
    m_simpleShadowLabel->setText(tr("%1%").arg(shadowPercent));
    m_simpleShadowSlider->setEnabled(state.castsShadow);

    constexpr std::array<core::Vec3, 5> sourceOffsets{{
        {-4.0f, 4.0f, 5.0f},
        {0.0f, 4.0f, 5.0f},
        {4.0f, 4.0f, 5.0f},
        {0.0f, 4.0f, -5.0f},
        {0.0f, 7.0f, 0.1f},
    }};
    const core::Vec3 sourceDirection =
        normalized({-state.direction.x, -state.direction.y, -state.direction.z});
    int bestDirection = 0;
    float bestDot = -2.0f;
    for (int i = 0; i < static_cast<int>(sourceOffsets.size()); ++i) {
        const core::Vec3 candidate = normalized(sourceOffsets[static_cast<size_t>(i)]);
        const float dot = sourceDirection.x * candidate.x +
                          sourceDirection.y * candidate.y +
                          sourceDirection.z * candidate.z;
        if (dot > bestDot) {
            bestDot = dot;
            bestDirection = i;
        }
    }
    m_simpleDirectionCombo->setCurrentIndex(bestDirection);
}

void PrevizLightingWindow::applySimpleDirection(int directionIndex) {
    if (m_updating) return;
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    constexpr std::array<core::Vec3, 5> sourceOffsets{{
        {-4.0f, 4.0f, 5.0f},
        {0.0f, 4.0f, 5.0f},
        {4.0f, 4.0f, 5.0f},
        {0.0f, 4.0f, -5.0f},
        {0.0f, 7.0f, 0.1f},
    }};
    if (directionIndex < 0 ||
        directionIndex >= static_cast<int>(sourceOffsets.size())) {
        return;
    }
    const core::Vec3 target = m_hasAimTarget ? m_aimTarget : core::Vec3{0.0f, 1.0f, 0.0f};
    const core::Vec3 offset = sourceOffsets[static_cast<size_t>(directionIndex)];
    core::PrevizLightState& state = editableState(*light);
    state.position = {target.x + offset.x, target.y + offset.y,
                      target.z + offset.z};
    state.direction = normalized(
        {target.x - state.position.x, target.y - state.position.y,
         target.z - state.position.z});
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::applySimpleIntensity(int percent) {
    if (m_updating) return;
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    core::PrevizLightState& state = editableState(*light);
    state.intensity = simpleIntensityReference(state.type) *
                      static_cast<float>(percent) / 100.0f;
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::applySimpleAmbient(int percent) {
    if (m_updating || !m_scene) return;
    m_scene->ambientIntensity =
        std::clamp(static_cast<float>(percent) / 200.0f, 0.0f, 0.5f);
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::applySimpleShadowEnabled(bool enabled) {
    if (m_updating) return;
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    editableState(*light).castsShadow = enabled;
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::applySimpleShadowOpacity(int percent) {
    if (m_updating) return;
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    editableState(*light).shadowOpacity =
        std::clamp(static_cast<float>(percent) / 100.0f, 0.0f, 1.0f);
    refreshControls();
    emitLightingEdited();
}

bool PrevizLightingWindow::debugExerciseSimpleControls() {
    if (!m_scene || !primaryLight()) return false;
    applySimpleDirection(0);
    applySimpleIntensity(80);
    applySimpleAmbient(40);
    applySimpleShadowEnabled(true);
    applySimpleShadowOpacity(60);

    const core::PrevizLightState state = primaryLight()->stateAt(m_frame);
    const float expectedIntensity =
        simpleIntensityReference(state.type) * 0.8f;
    return std::abs(state.intensity - expectedIntensity) < 0.001f &&
           std::abs(m_scene->ambientIntensity - 0.2f) < 0.001f &&
           state.castsShadow &&
           std::abs(state.shadowOpacity - 0.6f) < 0.001f &&
           state.direction.x > 0.2f;
}

void PrevizLightingWindow::chooseSimpleLightColor() {
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    core::PrevizLightState& state = editableState(*light);
    const QColor color =
        QColorDialog::getColor(colorFromVec(state.color), this, tr("光の色"));
    if (!color.isValid()) return;
    state.color = vecFromColor(color);
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::setDetailsVisible(bool visible) {
    if (!m_advancedPanel || !m_detailsButton) return;
    {
        const QSignalBlocker blocker(m_detailsButton);
        m_detailsButton->setChecked(visible);
    }
    if (m_simplePanel) m_simplePanel->setVisible(!visible);
    m_advancedPanel->setVisible(visible);
    m_detailsButton->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
    m_detailsButton->setText(visible ? tr("詳細設定を閉じる")
                                     : tr("詳細設定..."));
    if (visible) {
        resize(std::max(width(), 760), std::max(height(), 700));
    } else {
        resize(std::min(width(), 560), 320);
    }
}

void PrevizLightingWindow::applyControls() {
    if (m_updating) return;
    core::PrevizLight* light = selectedLight();
    if (!light) return;
    core::PrevizLightState& state = editableState(*light);
    state.enabled = m_enabledCheck->isChecked();
    state.type = static_cast<core::PrevizLightType>(m_typeCombo->currentIndex());
    state.intensity =
        std::clamp(static_cast<float>(m_intensitySpin->value()), 0.0f, 20.0f);
    state.position = {static_cast<float>(m_posX->value()),
                      static_cast<float>(m_posY->value()),
                      static_cast<float>(m_posZ->value())};
    const core::Vec3 target{static_cast<float>(m_targetX->value()),
                            static_cast<float>(m_targetY->value()),
                            static_cast<float>(m_targetZ->value())};
    state.direction =
        normalized({target.x - state.position.x, target.y - state.position.y,
                    target.z - state.position.z},
                   state.direction);
    state.rangeMeters =
        std::max(0.1f, static_cast<float>(m_rangeSpin->value()));
    state.coneAngleDeg =
        std::clamp(static_cast<float>(m_coneSpin->value()), 2.0f, 175.0f);
    state.castsShadow = m_shadowCheck->isChecked();
    state.shadowOpacity =
        std::clamp(static_cast<float>(m_shadowOpacitySpin->value()), 0.0f, 1.0f);
    state.shadowSoftness =
        std::clamp(static_cast<float>(m_shadowSoftnessSpin->value()), 0.0f, 1.0f);
    if (QListWidgetItem* item = m_list->currentItem()) {
        const QSignalBlocker blocker(m_list);
        item->setCheckState(state.enabled ? Qt::Checked : Qt::Unchecked);
    }
    emitLightingEdited();
}

void PrevizLightingWindow::chooseLightColor() {
    core::PrevizLight* light = selectedLight();
    if (!light) return;
    core::PrevizLightState& state = editableState(*light);
    const QColor color =
        QColorDialog::getColor(colorFromVec(state.color), this, tr("光の色"));
    if (!color.isValid()) return;
    state.color = vecFromColor(color);
    setColorButton(m_colorButton, state.color);
    emitLightingEdited();
}

void PrevizLightingWindow::chooseAmbientColor() {
    if (!m_scene) return;
    const QColor color = QColorDialog::getColor(
        colorFromVec(m_scene->ambientColor), this, tr("環境光の色"));
    if (!color.isValid()) return;
    m_scene->ambientColor = vecFromColor(color);
    setColorButton(m_ambientColorButton, m_scene->ambientColor);
    emitLightingEdited();
}

void PrevizLightingWindow::addLight(core::PrevizLightType type) {
    if (!m_scene) return;
    if (m_scene->lights.size() >= kMaxRealtimeLights) {
        QMessageBox::information(this, tr("光源を追加"),
                                 tr("リアルタイム照明は4灯まで使用できます。"));
        return;
    }
    core::PrevizLight light;
    light.name = tr("ライト %1").arg(m_scene->lights.size() + 1).toStdString();
    light.state.type = type;
    light.state.position = {3.0f, 4.0f, 3.0f};
    light.state.direction = normalized({-3.0f, -3.0f, -3.0f});
    if (type == core::PrevizLightType::Point) light.state.castsShadow = false;
    m_scene->lights.push_back(std::move(light));
    rebuildList();
    m_list->setCurrentRow(static_cast<int>(m_scene->lights.size()) - 1);
    emitLightingEdited();
}

void PrevizLightingWindow::duplicateLight() {
    if (!m_scene) return;
    core::PrevizLight* light = selectedLight();
    if (!light) return;
    if (m_scene->lights.size() >= kMaxRealtimeLights) {
        QMessageBox::information(this, tr("光源を複製"),
                                 tr("リアルタイム照明は4灯まで使用できます。"));
        return;
    }
    const int row = selectedLightIndex();
    core::PrevizLight copy = *light;
    copy.name += " コピー";
    m_scene->lights.insert(m_scene->lights.begin() + row + 1, std::move(copy));
    rebuildList();
    m_list->setCurrentRow(row + 1);
    emitLightingEdited();
}

void PrevizLightingWindow::removeLight() {
    if (!m_scene) return;
    const int row = selectedLightIndex();
    if (row < 0 || row >= static_cast<int>(m_scene->lights.size())) return;
    m_scene->lights.erase(m_scene->lights.begin() + row);
    rebuildList();
    emitLightingEdited();
}

void PrevizLightingWindow::aimAtTarget() {
    if (!m_hasAimTarget || !selectedLight()) return;
    m_targetX->setValue(m_aimTarget.x);
    m_targetY->setValue(m_aimTarget.y);
    m_targetZ->setValue(m_aimTarget.z);
    applyControls();
}

void PrevizLightingWindow::aimPrimaryAtTarget() {
    if (!m_hasAimTarget) return;
    core::PrevizLight* light = primaryLight();
    if (!light) return;
    core::PrevizLightState& state = editableState(*light);
    state.direction =
        normalized({m_aimTarget.x - state.position.x,
                    m_aimTarget.y - state.position.y,
                    m_aimTarget.z - state.position.z},
                   state.direction);
    refreshControls();
    emitLightingEdited();
}

void PrevizLightingWindow::updateControlAvailability() {
    const bool hasLight = selectedLight() != nullptr;
    const auto type = static_cast<core::PrevizLightType>(m_typeCombo->currentIndex());
    const bool positional = type != core::PrevizLightType::Directional;
    const bool spot = type == core::PrevizLightType::Spot;
    for (QDoubleSpinBox* spin : {m_posX, m_posY, m_posZ}) {
        spin->setEnabled(hasLight);
    }
    m_rangeSpin->setEnabled(hasLight && positional);
    m_coneSpin->setEnabled(hasLight && spot);
    m_aimButton->setEnabled(hasLight && m_hasAimTarget);
    const bool shadow = hasLight && m_shadowCheck->isChecked();
    m_shadowOpacitySpin->setEnabled(shadow);
    m_shadowSoftnessSpin->setEnabled(shadow);
    if (m_simpleShadowSlider) {
        m_simpleShadowSlider->setEnabled(primaryLight() &&
                                         m_simpleShadowCheck->isChecked());
    }
}

void PrevizLightingWindow::emitLightingEdited() {
    emit lightingEdited();
}

void PrevizLightingWindow::applyPreset(int presetIndex) {
    if (!m_scene) return;
    const core::Vec3 target{0.0f, 1.0f, 0.0f};
    m_scene->lights.clear();
    if (presetIndex == 1) {
        m_scene->ambientColor = {0.78f, 0.86f, 1.0f};
        m_scene->ambientIntensity = 0.28f;
        core::PrevizLight sun =
            makeLight("太陽", core::PrevizLightType::Directional,
                      {1.0f, 0.92f, 0.78f}, 1.45f, {5.0f, 8.0f, 4.0f},
                      target, true);
        sun.state.shadowOpacity = 0.48f;
        sun.state.shadowSoftness = 0.18f;
        m_scene->lights.push_back(std::move(sun));
    } else if (presetIndex == 2) {
        m_scene->ambientColor = {0.20f, 0.28f, 0.55f};
        m_scene->ambientIntensity = 0.14f;
        core::PrevizLight moon =
            makeLight("月光", core::PrevizLightType::Directional,
                      {0.38f, 0.52f, 1.0f}, 0.8f, {-4.0f, 7.0f, -3.0f},
                      target, true);
        moon.state.shadowSoftness = 0.45f;
        core::PrevizLight practical =
            makeLight("室内灯", core::PrevizLightType::Point,
                      {1.0f, 0.48f, 0.18f}, 4.0f, {2.5f, 2.8f, 2.0f},
                      target, false);
        practical.state.rangeMeters = 8.0f;
        m_scene->lights = {std::move(moon), std::move(practical)};
    } else {
        m_scene->ambientColor = {0.72f, 0.78f, 0.92f};
        m_scene->ambientIntensity = 0.18f;
        core::PrevizLight key =
            makeLight("キー", core::PrevizLightType::Spot,
                      {1.0f, 0.78f, 0.62f}, 4.2f, {4.0f, 5.0f, 5.0f},
                      target, true);
        key.state.rangeMeters = 15.0f;
        key.state.coneAngleDeg = 55.0f;
        core::PrevizLight fill =
            makeLight("フィル", core::PrevizLightType::Point,
                      {0.48f, 0.66f, 1.0f}, 1.6f, {-4.0f, 3.0f, 3.0f},
                      target, false);
        fill.state.rangeMeters = 12.0f;
        core::PrevizLight rim =
            makeLight("リム", core::PrevizLightType::Spot,
                      {0.75f, 0.86f, 1.0f}, 3.0f, {0.0f, 4.0f, -5.0f},
                      target, false);
        rim.state.rangeMeters = 15.0f;
        rim.state.coneAngleDeg = 48.0f;
        m_scene->lights = {std::move(key), std::move(fill), std::move(rim)};
    }
    {
        const QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->setCurrentIndex(std::clamp(presetIndex, 0, 2));
    }
    rebuildList();
    refreshControls();
    emitLightingEdited();
}
