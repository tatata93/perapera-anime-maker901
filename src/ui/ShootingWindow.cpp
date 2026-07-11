#include "ShootingWindow.h"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "core/Compositor.h"
#include "core/Project.h"

namespace {

// パラメータ名(core::Effect::params のキー)の日本語表示名(旧撮影パネルから引き継ぎ)
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

// エフェクト行の表示名(「ブラー (全体)」「グロー (A)」等)
QString effectRowLabel(const core::Effect& effect, const QStringList& celNames) {
    QString target = QObject::tr("全体");
    if (effect.targetCel >= 0 && effect.targetCel < celNames.size()) target = celNames.at(effect.targetCel);
    return QObject::tr("%1 (%2)").arg(QString::fromUtf8(core::effectTypeName(effect.type)), target);
}

// プレビューの表示解像度
constexpr int kPreviewWidth = 480;
constexpr int kPreviewHeight = 270;

}  // namespace

ShootingWindow::ShootingWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("撮影 - perapera-anime-maker901"));
    resize(1280, 640);

    // ツールバー: カット選択+更新
    QToolBar* toolBar = addToolBar(tr("撮影"));
    toolBar->setMovable(false);
    toolBar->addWidget(new QLabel(tr(" カット: "), toolBar));
    m_cutCombo = new QComboBox(toolBar);
    m_cutCombo->setMinimumWidth(160);
    toolBar->addWidget(m_cutCombo);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &ShootingWindow::refresh);
    connect(m_cutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) return;
        m_cutIndex = index;
        m_effectRow = -1;
        m_koma = 0;
        refresh();
    });

    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);

    // 左: エフェクト一覧+対象+パラメータ+キー操作
    auto* leftContainer = new QWidget(central);
    leftContainer->setFixedWidth(280);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(leftContainer);
    leftLayout->addWidget(m_list, 1);

    auto* addButton = new QPushButton(tr("追加"), leftContainer);
    auto* menu = new QMenu(addButton);
    const struct { core::EffectType type; const char* label; } kTypes[] = {
        {core::EffectType::Blur, "ブラー"},
        {core::EffectType::Glow, "グロー"},
        {core::EffectType::Para, "パラ"},
        {core::EffectType::Shake, "シェイク"},
    };
    for (const auto& entry : kTypes) {
        QAction* action = menu->addAction(QString::fromUtf8(entry.label));
        const int typeInt = static_cast<int>(entry.type);
        connect(action, &QAction::triggered, this, [this, typeInt] { addEffectOfType(typeInt); });
    }
    addButton->setMenu(menu);
    m_removeButton = new QPushButton(tr("削除"), leftContainer);
    m_upButton = new QPushButton(tr("上へ"), leftContainer);
    m_downButton = new QPushButton(tr("下へ"), leftContainer);
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addWidget(m_upButton);
    buttonRow->addWidget(m_downButton);
    leftLayout->addLayout(buttonRow);

    auto* targetRow = new QHBoxLayout();
    targetRow->addWidget(new QLabel(tr("対象:"), leftContainer));
    m_targetCombo = new QComboBox(leftContainer);
    m_targetCombo->setEnabled(false);
    targetRow->addWidget(m_targetCombo, 1);
    leftLayout->addLayout(targetRow);

    m_paramContainer = new QWidget(leftContainer);
    m_paramForm = new QFormLayout(m_paramContainer);
    m_paramForm->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_paramContainer);

    m_keyStateLabel = new QLabel(tr("キー: なし"), leftContainer);
    leftLayout->addWidget(m_keyStateLabel);
    m_addKeyButton = new QPushButton(tr("キー追加"), leftContainer);
    m_removeKeyButton = new QPushButton(tr("キー削除"), leftContainer);
    auto* keyRow = new QHBoxLayout();
    keyRow->addWidget(m_addKeyButton);
    keyRow->addWidget(m_removeKeyButton);
    leftLayout->addLayout(keyRow);

    mainLayout->addWidget(leftContainer);

    // 中央: 撮影シート(行=エフェクト、列=コマ)。キーのあるセルに●を表示する
    m_sheet = new QTableWidget(central);
    m_sheet->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sheet->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sheet->horizontalHeader()->setDefaultSectionSize(28);
    m_sheet->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    mainLayout->addWidget(m_sheet, 1);

    // 右: 選択コマのエフェクト適用済みプレビュー
    auto* rightContainer = new QWidget(central);
    rightContainer->setFixedWidth(kPreviewWidth + 8);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_previewLabel = new QLabel(rightContainer);
    m_previewLabel->setFixedSize(kPreviewWidth, kPreviewHeight);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral("background-color: black;"));
    rightLayout->addWidget(m_previewLabel);
    m_komaLabel = new QLabel(rightContainer);
    rightLayout->addWidget(m_komaLabel);

    // クラシック撮影(マルチプレーン撮影台)パネル。チェックOFF=デジタル合成(従来動作)
    m_multiplaneGroup = new QGroupBox(tr("クラシック撮影(マルチプレーン)"), rightContainer);
    m_multiplaneGroup->setCheckable(true);
    m_multiplaneGroup->setChecked(false);
    auto* mpLayout = new QVBoxLayout(m_multiplaneGroup);

    auto* mpForm = new QFormLayout();
    m_mpFocalSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFocalSpin->setRange(1.0, 300.0);
    m_mpFocalSpin->setDecimals(1);
    m_mpFocalSpin->setSuffix(tr(" mm"));
    m_mpFocalSpin->setFocusPolicy(Qt::ClickFocus);
    mpForm->addRow(tr("焦点距離"), m_mpFocalSpin);

    m_mpSensorSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpSensorSpin->setRange(1.0, 100.0);
    m_mpSensorSpin->setDecimals(1);
    m_mpSensorSpin->setSuffix(tr(" mm"));
    m_mpSensorSpin->setFocusPolicy(Qt::ClickFocus);
    mpForm->addRow(tr("センサー幅"), m_mpSensorSpin);

    m_mpFStopSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFStopSpin->setRange(0.0, 32.0);
    m_mpFStopSpin->setDecimals(1);
    m_mpFStopSpin->setSpecialValueText(tr("パンフォーカス"));
    m_mpFStopSpin->setFocusPolicy(Qt::ClickFocus);
    mpForm->addRow(tr("絞り(F値)"), m_mpFStopSpin);

    m_mpFocusSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFocusSpin->setRange(10.0, 10000.0);
    m_mpFocusSpin->setDecimals(1);
    m_mpFocusSpin->setSuffix(tr(" mm"));
    m_mpFocusSpin->setFocusPolicy(Qt::ClickFocus);
    mpForm->addRow(tr("フォーカス距離"), m_mpFocusSpin);

    m_mpSamplesSpin = new QSpinBox(m_multiplaneGroup);
    m_mpSamplesSpin->setRange(1, 64);
    m_mpSamplesSpin->setFocusPolicy(Qt::ClickFocus);
    mpForm->addRow(tr("サンプル数"), m_mpSamplesSpin);
    mpLayout->addLayout(mpForm);

    m_mpTable = new QTableWidget(m_multiplaneGroup);
    m_mpTable->setColumnCount(3);
    m_mpTable->setHorizontalHeaderLabels({tr("セル"), tr("距離mm"), tr("幅mm")});
    m_mpTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mpTable->verticalHeader()->setVisible(false);
    m_mpTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mpTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mpTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mpTable->setMaximumHeight(140);
    mpLayout->addWidget(m_mpTable);

    auto* mpButtonRow = new QHBoxLayout();
    m_mpAddButton = new QPushButton(tr("段を追加"), m_multiplaneGroup);
    m_mpRemoveButton = new QPushButton(tr("段を削除"), m_multiplaneGroup);
    mpButtonRow->addWidget(m_mpAddButton);
    mpButtonRow->addWidget(m_mpRemoveButton);
    mpLayout->addLayout(mpButtonRow);

    rightLayout->addWidget(m_multiplaneGroup);
    rightLayout->addStretch(1);
    mainLayout->addWidget(rightContainer);

    setCentralWidget(central);

    connect(m_list, &QListWidget::itemChanged, this, &ShootingWindow::onListCheckChanged);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating) return;
        m_effectRow = row;
        m_pendingParams.clear();
        // シートの選択セルも追従させる(currentCellChangedの再入はm_updatingで抑止)
        m_updating = true;
        if (row >= 0 && m_sheet->rowCount() > row) m_sheet->setCurrentCell(row, m_koma);
        m_updating = false;
        syncSelectionUI();
    });
    connect(m_removeButton, &QPushButton::clicked, this, &ShootingWindow::removeSelected);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveSelected(+1); });
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ShootingWindow::onTargetIndexChanged);
    connect(m_addKeyButton, &QPushButton::clicked, this, &ShootingWindow::addKeyAtCurrentKoma);
    connect(m_removeKeyButton, &QPushButton::clicked, this, &ShootingWindow::removeKeyAtCurrentKoma);
    connect(m_sheet, &QTableWidget::currentCellChanged, this,
            [this](int row, int column, int, int) { onSheetCellChanged(row, column); });
    connect(m_sheet, &QTableWidget::cellDoubleClicked, this, &ShootingWindow::onSheetCellDoubleClicked);

    connect(m_multiplaneGroup, &QGroupBox::toggled, this, &ShootingWindow::onMultiplaneToggled);
    connect(m_mpFocalSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpSensorSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpFStopSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpFocusSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpSamplesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { onMultiplaneCameraChanged(); });
    connect(m_mpAddButton, &QPushButton::clicked, this, &ShootingWindow::addMultiplanePlaneRow);
    connect(m_mpRemoveButton, &QPushButton::clicked, this, &ShootingWindow::removeMultiplanePlaneRow);
}

void ShootingWindow::setProject(core::Project* project) {
    m_project = project;
    m_cutIndex = 0;
    m_effectRow = -1;
    m_koma = 0;
    m_pendingParams.clear();
}

void ShootingWindow::setCanvasSize(int width, int height) {
    m_canvasWidth = width;
    m_canvasHeight = height;
}

void ShootingWindow::setCutIndex(int index) {
    if (index == m_cutIndex) return;
    m_cutIndex = index;
    m_effectRow = -1;
    m_koma = 0;
    refresh();
}

core::Cut* ShootingWindow::currentCut() const {
    if (!m_project || m_project->sceneCount() == 0) return nullptr;
    core::Scene& scene = m_project->scene(0);
    if (m_cutIndex < 0 || static_cast<size_t>(m_cutIndex) >= scene.cutCount()) return nullptr;
    return &scene.cut(static_cast<size_t>(m_cutIndex));
}

void ShootingWindow::refresh() {
    m_updating = true;
    m_cutCombo->clear();
    if (m_project && m_project->sceneCount() > 0) {
        core::Scene& scene = m_project->scene(0);
        for (size_t i = 0; i < scene.cutCount(); ++i) {
            m_cutCombo->addItem(QString::fromStdString(scene.cut(i).name()));
        }
        m_cutIndex = std::clamp(m_cutIndex, 0, static_cast<int>(scene.cutCount()) - 1);
        m_cutCombo->setCurrentIndex(m_cutIndex);
    }
    m_updating = false;

    core::Cut* cut = currentCut();
    if (cut) {
        m_koma = std::clamp(m_koma, 0, static_cast<int>(cut->frameCount()) - 1);
        m_effectRow = std::min(m_effectRow, static_cast<int>(cut->effects().size()) - 1);
    } else {
        m_koma = 0;
        m_effectRow = -1;
    }
    m_pendingParams.clear();

    rebuildEffectList();
    rebuildSheet();
    syncSelectionUI();
    rebuildMultiplanePanel();
}

void ShootingWindow::rebuildEffectList() {
    m_updating = true;
    m_list->clear();
    core::Cut* cut = currentCut();
    if (cut) {
        QStringList celNames;
        for (size_t ci = 0; ci < cut->celCount(); ++ci) {
            celNames.append(QString::fromStdString(cut->cel(ci).name()));
        }
        for (const core::Effect& effect : cut->effects()) {
            auto* item = new QListWidgetItem(effectRowLabel(effect, celNames));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(effect.enabled ? Qt::Checked : Qt::Unchecked);
            m_list->addItem(item);
        }
        m_list->setCurrentRow(m_effectRow);
    }
    m_updating = false;
}

void ShootingWindow::rebuildSheet() {
    m_updating = true;
    core::Cut* cut = currentCut();
    if (!cut) {
        m_sheet->setRowCount(0);
        m_sheet->setColumnCount(0);
        m_updating = false;
        return;
    }

    const int rows = static_cast<int>(cut->effects().size());
    const int cols = static_cast<int>(cut->frameCount());
    m_sheet->setRowCount(rows);
    m_sheet->setColumnCount(cols);

    QStringList celNames;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) {
        celNames.append(QString::fromStdString(cut->cel(ci).name()));
    }
    QStringList rowLabels;
    for (const core::Effect& effect : cut->effects()) rowLabels.append(effectRowLabel(effect, celNames));
    m_sheet->setVerticalHeaderLabels(rowLabels);

    QStringList colLabels;
    for (int c = 0; c < cols; ++c) colLabels.append(QString::number(c + 1));
    m_sheet->setHorizontalHeaderLabels(colLabels);

    for (int r = 0; r < rows; ++r) {
        const core::Effect& effect = cut->effects()[static_cast<size_t>(r)];
        for (int c = 0; c < cols; ++c) {
            const bool hasKey = effect.paramKeys.count(static_cast<size_t>(c)) > 0;
            auto* item = new QTableWidgetItem(hasKey ? QStringLiteral("●") : QString());
            item->setTextAlignment(Qt::AlignCenter);
            m_sheet->setItem(r, c, item);
        }
    }

    if (m_effectRow >= 0 && m_effectRow < rows && m_koma < cols) {
        m_sheet->setCurrentCell(m_effectRow, m_koma);
    }
    m_updating = false;
}

void ShootingWindow::syncSelectionUI() {
    m_updating = true;
    core::Cut* cut = currentCut();
    const bool hasSelection =
        cut && m_effectRow >= 0 && m_effectRow < static_cast<int>(cut->effects().size());

    m_targetCombo->clear();
    m_targetCombo->addItem(tr("全体"));
    if (cut) {
        for (size_t ci = 0; ci < cut->celCount(); ++ci) {
            m_targetCombo->addItem(QString::fromStdString(cut->cel(ci).name()));
        }
    }
    m_targetCombo->setEnabled(hasSelection);
    if (hasSelection) {
        const core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];
        int comboIndex = 0;
        if (effect.targetCel >= 0 && effect.targetCel < static_cast<int>(cut->celCount())) {
            comboIndex = effect.targetCel + 1;
        }
        m_targetCombo->setCurrentIndex(comboIndex);
    }

    m_removeButton->setEnabled(hasSelection);
    m_upButton->setEnabled(hasSelection && m_effectRow > 0);
    m_downButton->setEnabled(hasSelection &&
                             m_effectRow + 1 < static_cast<int>(cut ? cut->effects().size() : 0));
    m_addKeyButton->setEnabled(hasSelection);
    m_removeKeyButton->setEnabled(hasSelection);

    m_updating = false;

    rebuildParamForm();
    updateKeyStateLabel();
    updatePreview();
}

void ShootingWindow::rebuildParamForm() {
    while (m_paramForm->rowCount() > 0) m_paramForm->removeRow(0);
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;

    // 選択中コマで有効なパラメータ(キー補間込み)を表示する。保留値があればそれを優先
    const core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];
    std::map<std::string, double> shown = effect.paramsAt(static_cast<size_t>(m_koma));
    for (const auto& [key, value] : m_pendingParams) shown[key] = value;

    m_updating = true;
    for (const auto& [key, value] : shown) {
        auto* spin = new QDoubleSpinBox(m_paramContainer);
        spin->setRange(0.0, 4096.0);
        spin->setDecimals(1);
        spin->setSingleStep(isDensityParam(key) ? 0.05 : 1.0);
        spin->setValue(value);
        spin->setFocusPolicy(Qt::ClickFocus);
        const std::string keyCopy = key;
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, keyCopy](double v) { onParamValueChanged(keyCopy, v); });
        m_paramForm->addRow(paramLabel(key), spin);
    }
    m_updating = false;
}

void ShootingWindow::updateKeyStateLabel() {
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) {
        m_keyStateLabel->setText(tr("キー: -"));
        return;
    }
    const core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];
    if (effect.paramKeys.empty()) {
        m_keyStateLabel->setText(tr("キー: なし(スピンで基本値を直接編集)"));
    } else if (effect.paramKeys.count(static_cast<size_t>(m_koma)) > 0) {
        m_keyStateLabel->setText(tr("キー: あり(コマ%1)").arg(m_koma + 1));
    } else {
        m_keyStateLabel->setText(tr("キー: なし(補間中。キー追加で確定)"));
    }
}

void ShootingWindow::updatePreview() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_previewLabel->clear();
        m_komaLabel->clear();
        return;
    }
    const core::Bitmap bitmap =
        core::renderCutFrame(*cut, static_cast<size_t>(m_koma), m_canvasWidth, m_canvasHeight);
    const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    // scaled()は新しいQImageを作る(元のピクセルへの参照が切れる)ため、bitmapの寿命外でも安全
    m_previewLabel->setPixmap(QPixmap::fromImage(
        image.scaled(kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_komaLabel->setText(tr("コマ %1 / %2").arg(m_koma + 1).arg(cut->frameCount()));
}

void ShootingWindow::addEffectOfType(int typeInt) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::Effect effect;
    effect.type = static_cast<core::EffectType>(typeInt);
    effect.enabled = true;
    effect.targetCel = -1;  // 既定は全体
    effect.params = core::effectDefaultParams(effect.type);
    cut->effects().push_back(std::move(effect));
    m_effectRow = static_cast<int>(cut->effects().size()) - 1;
    m_pendingParams.clear();
    rebuildEffectList();
    rebuildSheet();
    syncSelectionUI();
    emit edited();
}

void ShootingWindow::removeSelected() {
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;
    cut->effects().erase(cut->effects().begin() + m_effectRow);
    m_effectRow = cut->effects().empty()
                      ? -1
                      : std::min(m_effectRow, static_cast<int>(cut->effects().size()) - 1);
    m_pendingParams.clear();
    rebuildEffectList();
    rebuildSheet();
    syncSelectionUI();
    emit edited();
}

void ShootingWindow::moveSelected(int delta) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    const int newRow = m_effectRow + delta;
    if (m_effectRow < 0 || newRow < 0 || newRow >= static_cast<int>(cut->effects().size())) return;
    std::swap(cut->effects()[static_cast<size_t>(m_effectRow)], cut->effects()[static_cast<size_t>(newRow)]);
    m_effectRow = newRow;
    rebuildEffectList();
    rebuildSheet();
    syncSelectionUI();
    emit edited();
}

void ShootingWindow::onListCheckChanged(QListWidgetItem* item) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    const int row = m_list->row(item);
    if (!cut || row < 0 || row >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(row)].enabled = (item->checkState() == Qt::Checked);
    updatePreview();
    emit edited();
}

void ShootingWindow::onTargetIndexChanged(int index) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(m_effectRow)].targetCel = index <= 0 ? -1 : index - 1;
    rebuildEffectList();  // 対象表示テキストが変わるので一覧・シート行ヘッダを作り直す
    rebuildSheet();
    updatePreview();
    emit edited();
}

void ShootingWindow::onParamValueChanged(const std::string& key, double value) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];

    if (effect.paramKeys.empty()) {
        // キーが無いエフェクトは基本値を直接編集する(従来動作)
        effect.params[key] = value;
        updatePreview();
        emit edited();
    } else {
        // キー持ちエフェクトはスピンだけではデータを変えず、「キー追加」で確定する
        m_pendingParams[key] = value;
        updateKeyStateLabel();
    }
}

void ShootingWindow::addKeyAtCurrentKoma() {
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];

    // 現在の補間値に保留中のスピン編集を重ねたものをキーとして確定する
    std::map<std::string, double> keyParams = effect.paramsAt(static_cast<size_t>(m_koma));
    for (const auto& [key, value] : m_pendingParams) keyParams[key] = value;
    effect.paramKeys[static_cast<size_t>(m_koma)] = std::move(keyParams);
    m_pendingParams.clear();

    rebuildSheet();
    syncSelectionUI();
    emit edited();
}

void ShootingWindow::removeKeyAtCurrentKoma() {
    core::Cut* cut = currentCut();
    if (!cut || m_effectRow < 0 || m_effectRow >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(m_effectRow)];
    if (effect.paramKeys.erase(static_cast<size_t>(m_koma)) == 0) return;
    m_pendingParams.clear();

    rebuildSheet();
    syncSelectionUI();
    emit edited();
}

void ShootingWindow::onSheetCellChanged(int row, int column) {
    if (m_updating || row < 0 || column < 0) return;
    m_effectRow = row;
    m_koma = column;
    m_pendingParams.clear();
    // エフェクト一覧の選択も追従させる(再入はm_updatingで抑止)
    m_updating = true;
    m_list->setCurrentRow(row);
    m_updating = false;
    syncSelectionUI();
}

void ShootingWindow::onSheetCellDoubleClicked(int row, int column) {
    core::Cut* cut = currentCut();
    if (!cut || row < 0 || row >= static_cast<int>(cut->effects().size())) return;
    m_effectRow = row;
    m_koma = column;
    core::Effect& effect = cut->effects()[static_cast<size_t>(row)];
    if (effect.paramKeys.count(static_cast<size_t>(column)) > 0) {
        removeKeyAtCurrentKoma();  // キーがあればトグルで削除
    } else {
        addKeyAtCurrentKoma();  // 無ければ現在の補間値でキーを打つ
    }
}

void ShootingWindow::debugSelectKoma(int koma) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    m_koma = std::clamp(koma, 0, static_cast<int>(cut->frameCount()) - 1);
    if (m_effectRow < 0 && !cut->effects().empty()) m_effectRow = 0;
    if (m_effectRow >= 0) {
        m_updating = true;
        m_sheet->setCurrentCell(m_effectRow, m_koma);
        m_list->setCurrentRow(m_effectRow);
        m_updating = false;
    }
    syncSelectionUI();
}

void ShootingWindow::rebuildMultiplanePanel() {
    m_updating = true;
    core::Cut* cut = currentCut();
    const core::MultiplaneSetup setup = cut ? cut->multiplane() : core::MultiplaneSetup{};

    m_multiplaneGroup->setEnabled(cut != nullptr);
    m_multiplaneGroup->setChecked(setup.enabled);
    m_mpFocalSpin->setValue(setup.camera.focalLengthMm);
    m_mpSensorSpin->setValue(setup.camera.sensorWidthMm);
    m_mpFStopSpin->setValue(setup.camera.apertureFStop);
    m_mpFocusSpin->setValue(setup.camera.focusDistanceMm);
    m_mpSamplesSpin->setValue(setup.samplesPerPixel);

    QStringList celNames;
    if (cut) {
        for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));
    }

    m_mpTable->setRowCount(static_cast<int>(setup.planes.size()));
    for (int r = 0; r < static_cast<int>(setup.planes.size()); ++r) {
        const core::MultiplaneCelPlane& plane = setup.planes[static_cast<size_t>(r)];

        auto* combo = new QComboBox(m_mpTable);
        combo->addItems(celNames);
        if (plane.celIndex >= 0 && plane.celIndex < celNames.size()) combo->setCurrentIndex(plane.celIndex);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, r](int index) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].celIndex = index;
            updatePreview();
            emit edited();
        });
        m_mpTable->setCellWidget(r, 0, combo);

        auto* distSpin = new QDoubleSpinBox(m_mpTable);
        distSpin->setRange(1.0, 100000.0);
        distSpin->setDecimals(1);
        distSpin->setValue(plane.distanceMm);
        distSpin->setFocusPolicy(Qt::ClickFocus);
        connect(distSpin, &QDoubleSpinBox::valueChanged, this, [this, r](double value) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].distanceMm = value;
            updatePreview();
            emit edited();
        });
        m_mpTable->setCellWidget(r, 1, distSpin);

        auto* widthSpin = new QDoubleSpinBox(m_mpTable);
        widthSpin->setRange(1.0, 100000.0);
        widthSpin->setDecimals(1);
        widthSpin->setValue(plane.widthMm);
        widthSpin->setFocusPolicy(Qt::ClickFocus);
        connect(widthSpin, &QDoubleSpinBox::valueChanged, this, [this, r](double value) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].widthMm = value;
            updatePreview();
            emit edited();
        });
        m_mpTable->setCellWidget(r, 2, widthSpin);
    }

    m_mpAddButton->setEnabled(cut != nullptr);
    m_mpRemoveButton->setEnabled(!setup.planes.empty());
    m_updating = false;
}

void ShootingWindow::onMultiplaneToggled(bool checked) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().enabled = checked;
    updatePreview();
    emit edited();
}

void ShootingWindow::onMultiplaneCameraChanged() {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();
    setup.camera.focalLengthMm = m_mpFocalSpin->value();
    setup.camera.sensorWidthMm = m_mpSensorSpin->value();
    setup.camera.apertureFStop = m_mpFStopSpin->value();
    setup.camera.focusDistanceMm = m_mpFocusSpin->value();
    setup.samplesPerPixel = m_mpSamplesSpin->value();
    updatePreview();
    emit edited();
}

void ShootingWindow::addMultiplanePlaneRow() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();

    // 未割付(planesに無い)の先頭セルを既定対象にする。全セル割付済みならセル0のまま
    int celIndex = 0;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) {
        const bool assigned =
            std::any_of(setup.planes.begin(), setup.planes.end(),
                        [ci](const core::MultiplaneCelPlane& p) { return p.celIndex == static_cast<int>(ci); });
        if (!assigned) {
            celIndex = static_cast<int>(ci);
            break;
        }
    }
    core::MultiplaneCelPlane plane;
    plane.celIndex = celIndex;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    setup.planes.push_back(plane);

    rebuildMultiplanePanel();
    updatePreview();
    emit edited();
}

void ShootingWindow::removeMultiplanePlaneRow() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();
    if (setup.planes.empty()) return;

    const int row = m_mpTable->currentRow();
    const int target =
        (row >= 0 && row < static_cast<int>(setup.planes.size())) ? row : static_cast<int>(setup.planes.size()) - 1;
    setup.planes.erase(setup.planes.begin() + target);

    rebuildMultiplanePanel();
    updatePreview();
    emit edited();
}
