#include "EffectPanel.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <map>
#include <utility>

namespace {

// パラメータ名(core::Effect::params のキー)の日本語表示名
QString paramLabel(const std::string& key) {
    static const std::map<std::string, QString> kLabels = {
        {"radius", QObject::tr("半径")},     {"threshold", QObject::tr("しきい値")},
        {"strength", QObject::tr("強さ")},   {"top", QObject::tr("上濃度")},
        {"bottom", QObject::tr("下濃度")},   {"r", QObject::tr("R")},
        {"g", QObject::tr("G")},             {"b", QObject::tr("B")},
        {"amplitudeX", QObject::tr("振幅X")}, {"amplitudeY", QObject::tr("振幅Y")},
        {"seed", QObject::tr("シード")},
    };
    const auto it = kLabels.find(key);
    if (it != kLabels.end()) return it->second;
    return QString::fromStdString(key);  // 未知のキーはそのまま表示(将来のパラメータ追加に備えた保険)
}

// 濃度系パラメータ(0〜1程度の細かい調整が必要)はステップ0.05、それ以外(半径系)は1.0
bool isDensityParam(const std::string& key) { return key == "top" || key == "bottom" || key == "strength"; }

}  // namespace

EffectPanel::EffectPanel(QWidget* parent) : QDockWidget(tr("撮影"), parent) {
    setObjectName(QStringLiteral("EffectPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    m_list = new QListWidget(container);
    layout->addWidget(m_list);

    auto* addButton = new QPushButton(tr("追加"), container);
    auto* menu = new QMenu(addButton);
    const struct { core::EffectType type; const char* label; } kTypes[] = {
        {core::EffectType::Blur, "ブラー"},
        {core::EffectType::Glow, "グロー"},
        {core::EffectType::Para, "パラ"},
        {core::EffectType::Shake, "シェイク"},
    };
    for (const auto& entry : kTypes) {
        QAction* action = menu->addAction(QString::fromUtf8(entry.label));
        const core::EffectType type = entry.type;
        connect(action, &QAction::triggered, this, [this, type] { addEffectOfType(type); });
    }
    addButton->setMenu(menu);

    m_removeButton = new QPushButton(tr("削除"), container);
    m_upButton = new QPushButton(tr("上へ"), container);
    m_downButton = new QPushButton(tr("下へ"), container);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addWidget(m_upButton);
    buttonRow->addWidget(m_downButton);
    layout->addLayout(buttonRow);

    auto* targetRow = new QHBoxLayout();
    targetRow->addWidget(new QLabel(tr("対象:"), container));
    m_targetCombo = new QComboBox(container);
    m_targetCombo->setEnabled(false);
    targetRow->addWidget(m_targetCombo, 1);
    layout->addLayout(targetRow);

    m_paramContainer = new QWidget(container);
    m_paramForm = new QFormLayout(m_paramContainer);
    m_paramForm->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_paramContainer);

    auto* previewButton = new QPushButton(tr("現在コマをプレビュー"), container);
    layout->addWidget(previewButton);

    layout->addStretch(1);
    setWidget(container);

    connect(m_list, &QListWidget::itemChanged, this, &EffectPanel::onCheckStateChanged);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { syncSelectionUI(); });
    connect(m_removeButton, &QPushButton::clicked, this, &EffectPanel::removeSelected);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveSelected(+1); });
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &EffectPanel::onTargetIndexChanged);
    connect(previewButton, &QPushButton::clicked, this, &EffectPanel::previewRequested);

    syncSelectionUI();  // 未選択状態でパラメータ欄・対象コンボを初期化する
}

void EffectPanel::setEffects(const std::vector<core::Effect>& effects, const QStringList& celNames) {
    const int previousRow = m_list->currentRow();
    m_effects = effects;
    m_celNames = celNames;
    rebuildList();
    const int row =
        (previousRow >= 0 && previousRow < static_cast<int>(m_effects.size())) ? previousRow : -1;
    m_list->setCurrentRow(row);
    syncSelectionUI();  // currentRowが変わらなかった場合(row==-1が維持される等)も確実に反映する
}

void EffectPanel::rebuildList() {
    m_updating = true;
    m_list->clear();
    for (const core::Effect& eff : m_effects) {
        QString targetText = tr("全体");
        if (eff.targetCel >= 0 && eff.targetCel < m_celNames.size()) targetText = m_celNames.at(eff.targetCel);
        auto* item =
            new QListWidgetItem(tr("%1 (%2)").arg(QString::fromUtf8(core::effectTypeName(eff.type)), targetText));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(eff.enabled ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(item);
    }
    m_updating = false;
}

void EffectPanel::syncSelectionUI() {
    m_updating = true;
    const int row = m_list->currentRow();
    const bool hasSelection = row >= 0 && row < static_cast<int>(m_effects.size());

    m_targetCombo->clear();
    m_targetCombo->addItem(tr("全体"));
    m_targetCombo->addItems(m_celNames);
    m_targetCombo->setEnabled(hasSelection);
    if (hasSelection) {
        const core::Effect& eff = m_effects[static_cast<size_t>(row)];
        int comboIndex = 0;
        if (eff.targetCel >= 0 && eff.targetCel < m_celNames.size()) comboIndex = eff.targetCel + 1;
        m_targetCombo->setCurrentIndex(comboIndex);
    }

    m_removeButton->setEnabled(hasSelection);
    m_upButton->setEnabled(hasSelection && row > 0);
    m_downButton->setEnabled(hasSelection && row + 1 < static_cast<int>(m_effects.size()));

    rebuildParamForm(hasSelection ? &m_effects[static_cast<size_t>(row)] : nullptr);
    m_updating = false;
}

void EffectPanel::rebuildParamForm(const core::Effect* effect) {
    while (m_paramForm->rowCount() > 0) m_paramForm->removeRow(0);
    if (!effect) return;
    for (const auto& [key, value] : effect->params) {
        auto* spin = new QDoubleSpinBox(m_paramContainer);
        spin->setRange(0.0, 4096.0);
        spin->setDecimals(1);
        spin->setSingleStep(isDensityParam(key) ? 0.05 : 1.0);
        spin->setValue(value);
        spin->setFocusPolicy(Qt::ClickFocus);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, key](double v) { onParamValueChanged(key, v); });
        m_paramForm->addRow(paramLabel(key), spin);
    }
}

void EffectPanel::addEffectOfType(core::EffectType type) {
    core::Effect eff;
    eff.type = type;
    eff.enabled = true;
    eff.targetCel = -1;  // 既定は全体
    eff.params = core::effectDefaultParams(type);
    m_effects.push_back(eff);
    rebuildList();
    m_list->setCurrentRow(static_cast<int>(m_effects.size()) - 1);
    emitEdited();
}

void EffectPanel::removeSelected() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_effects.size())) return;
    m_effects.erase(m_effects.begin() + row);
    rebuildList();
    const int newRow =
        m_effects.empty() ? -1 : std::min(row, static_cast<int>(m_effects.size()) - 1);
    m_list->setCurrentRow(newRow);
    if (newRow < 0) syncSelectionUI();  // 選択行が無い(setCurrentRow(-1)は変化なしとみなされ通知されない場合がある)
    emitEdited();
}

void EffectPanel::moveSelected(int delta) {
    const int row = m_list->currentRow();
    const int newRow = row + delta;
    if (row < 0 || newRow < 0 || newRow >= static_cast<int>(m_effects.size())) return;
    std::swap(m_effects[static_cast<size_t>(row)], m_effects[static_cast<size_t>(newRow)]);
    rebuildList();
    m_list->setCurrentRow(newRow);
    emitEdited();
}

void EffectPanel::onCheckStateChanged(QListWidgetItem* item) {
    if (m_updating) return;
    const int row = m_list->row(item);
    if (row < 0 || row >= static_cast<int>(m_effects.size())) return;
    m_effects[static_cast<size_t>(row)].enabled = (item->checkState() == Qt::Checked);
    emitEdited();
}

void EffectPanel::onTargetIndexChanged(int index) {
    if (m_updating) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_effects.size())) return;
    m_effects[static_cast<size_t>(row)].targetCel = index <= 0 ? -1 : index - 1;
    rebuildList();               // 対象表示テキストが変わるので一覧を作り直す
    m_list->setCurrentRow(row);  // 選択維持(内部でsyncSelectionUI()が再度呼ばれる)
    emitEdited();
}

void EffectPanel::onParamValueChanged(const std::string& key, double value) {
    if (m_updating) return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= static_cast<int>(m_effects.size())) return;
    m_effects[static_cast<size_t>(row)].params[key] = value;
    emitEdited();
}

void EffectPanel::emitEdited() { emit effectsEdited(); }

void EffectPanel::showPreview(const QImage& image) {
    if (!m_previewDialog) {
        m_previewDialog = new QDialog(this);
        m_previewDialog->setWindowTitle(tr("撮影プレビュー"));
        m_previewDialog->setAttribute(Qt::WA_DeleteOnClose);
        auto* dialogLayout = new QVBoxLayout(m_previewDialog);
        m_previewImageLabel = new QLabel(m_previewDialog);
        m_previewImageLabel->setAlignment(Qt::AlignCenter);
        dialogLayout->addWidget(m_previewImageLabel);
        connect(m_previewDialog, &QObject::destroyed, this, [this] {
            m_previewDialog = nullptr;
            m_previewImageLabel = nullptr;
        });
    }
    const QPixmap scaled =
        QPixmap::fromImage(image).scaled(960, 540, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewImageLabel->setPixmap(scaled);
    m_previewDialog->show();
    m_previewDialog->raise();
    m_previewDialog->activateWindow();
}
