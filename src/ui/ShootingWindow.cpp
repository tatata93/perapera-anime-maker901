#include "ShootingWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "core/Compositor.h"
#include "core/Project.h"

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

bool isRgbParam(const std::string& key) { return key == "r" || key == "g" || key == "b"; }

// エフェクトGroupBoxのタイトル(「ブラー (全体)」「グロー (A)」等)
QString effectRowLabel(const core::Effect& effect, const QStringList& celNames) {
    QString target = QObject::tr("全体");
    if (effect.targetCel >= 0 && effect.targetCel < celNames.size()) target = celNames.at(effect.targetCel);
    return QObject::tr("%1 (%2)").arg(QString::fromUtf8(core::effectTypeName(effect.type)), target);
}

// CTI(現在コマ)列のハイライト色
const QColor kCtiColumnColor(70, 110, 170);
const QColor kCtiHeaderColor(90, 140, 210);

}  // namespace

ShootingWindow::ShootingWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("撮影 - perapera-anime-maker901"));
    resize(1400, 900);

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
        m_koma = 0;
        refresh();
    });

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(4, 4, 4, 4);

    auto* vSplit = new QSplitter(Qt::Vertical, central);

    // ==== 上段: 左「エフェクトコントロール」/ 右「プレビュー」 ====
    auto* topSplit = new QSplitter(Qt::Horizontal, vSplit);

    // --- 左: エフェクトコントロールパネル(スクロール) ---
    m_effectScroll = new QScrollArea(topSplit);
    m_effectScroll->setWidgetResizable(true);
    m_effectScroll->setMinimumWidth(340);
    m_effectContainer = new QWidget();
    m_effectContainerLayout = new QVBoxLayout(m_effectContainer);
    m_effectContainerLayout->setAlignment(Qt::AlignTop);

    // 「エフェクトを追加」ボタン(エフェクトGroupBox群の下、常にレイアウトの固定位置に残す)
    m_addEffectButton = new QPushButton(tr("エフェクトを追加"), m_effectContainer);
    auto* addMenu = new QMenu(m_addEffectButton);
    const struct {
        core::EffectType type;
        const char* label;
    } kTypes[] = {
        {core::EffectType::Blur, "ブラー"},
        {core::EffectType::Glow, "グロー"},
        {core::EffectType::Para, "パラ"},
        {core::EffectType::Shake, "シェイク"},
    };
    for (const auto& entry : kTypes) {
        QAction* action = addMenu->addAction(QString::fromUtf8(entry.label));
        const int typeInt = static_cast<int>(entry.type);
        connect(action, &QAction::triggered, this, [this, typeInt] { addEffectOfType(typeInt); });
    }
    m_addEffectButton->setMenu(addMenu);
    m_effectContainerLayout->addWidget(m_addEffectButton);

    // クラシック撮影(マルチプレーン撮影台)パネル。追加ボタンのさらに下に配置する
    m_multiplaneGroup = new QGroupBox(tr("クラシック撮影(マルチプレーン)"), m_effectContainer);
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

    m_effectContainerLayout->addWidget(m_multiplaneGroup);

    m_effectScroll->setWidget(m_effectContainer);
    topSplit->addWidget(m_effectScroll);

    // --- 右: プレビュー+トランスポート ---
    auto* rightContainer = new QWidget(topSplit);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    m_previewLabel = new QLabel(rightContainer);
    m_previewLabel->setMinimumSize(640, 360);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral("background-color: black;"));
    rightLayout->addWidget(m_previewLabel, 1);

    auto* transportRow = new QHBoxLayout();
    m_playButton = new QPushButton(tr("再生"), rightContainer);
    transportRow->addWidget(m_playButton);
    m_komaLabel = new QLabel(rightContainer);
    transportRow->addWidget(m_komaLabel);
    transportRow->addStretch(1);
    rightLayout->addLayout(transportRow);

    topSplit->addWidget(rightContainer);
    topSplit->setStretchFactor(0, 0);
    topSplit->setStretchFactor(1, 1);
    topSplit->setSizes({380, 1000});

    vSplit->addWidget(topSplit);

    // ==== 下段: タイムライン ====
    auto* timelinePanel = new QWidget(vSplit);
    auto* timelineLayout = new QVBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(0, 4, 0, 0);
    timelineLayout->addWidget(new QLabel(tr("タイムライン"), timelinePanel));
    m_timeline = new QTableWidget(timelinePanel);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_timeline->setSelectionMode(QAbstractItemView::NoSelection);
    m_timeline->horizontalHeader()->setDefaultSectionSize(28);
    m_timeline->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    timelineLayout->addWidget(m_timeline);
    vSplit->addWidget(timelinePanel);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 1);
    vSplit->setSizes({640, 220});

    rootLayout->addWidget(vSplit);
    setCentralWidget(central);

    connect(m_playButton, &QPushButton::clicked, this, &ShootingWindow::togglePlayback);
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(1000 / 24);
    connect(m_playTimer, &QTimer::timeout, this, &ShootingWindow::onPlaybackTick);

    connect(m_timeline, &QTableWidget::cellClicked, this, &ShootingWindow::onTimelineCellClicked);
    connect(m_timeline, &QTableWidget::cellDoubleClicked, this, &ShootingWindow::onTimelineCellDoubleClicked);
    connect(m_timeline->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &ShootingWindow::onTimelineHeaderClicked);

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
    m_koma = 0;
}

void ShootingWindow::setCanvasSize(int width, int height) {
    m_canvasWidth = width;
    m_canvasHeight = height;
}

void ShootingWindow::setCutIndex(int index) {
    if (index == m_cutIndex) return;
    m_cutIndex = index;
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
    if (m_playTimer->isActive()) {
        m_playTimer->stop();
        m_playButton->setText(tr("再生"));
    }

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
    m_koma = cut ? std::clamp(m_koma, 0, static_cast<int>(cut->frameCount()) - 1) : 0;

    rebuildEffectControls();
    rebuildTimeline();
    rebuildMultiplanePanel();
    updatePreview();
    updateTransportLabel();
}

QGroupBox* ShootingWindow::buildEffectGroupBox(int effectIndex, const QStringList& celNames) {
    core::Cut* cut = currentCut();
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];

    auto* box = new QGroupBox(effectRowLabel(effect, celNames));
    box->setCheckable(true);
    box->setChecked(effect.enabled);
    connect(box, &QGroupBox::toggled, this,
            [this, effectIndex](bool checked) { onEffectEnabledChanged(effectIndex, checked); });

    auto* vlayout = new QVBoxLayout(box);

    // ヘッダ行: 対象コンボ + 上へ/下へ/削除の小ボタン
    auto* headerRow = new QHBoxLayout();
    headerRow->addWidget(new QLabel(tr("対象:"), box));
    auto* targetCombo = new QComboBox(box);
    targetCombo->addItem(tr("全体"));
    for (const QString& name : celNames) targetCombo->addItem(name);
    int comboIndex = 0;
    if (effect.targetCel >= 0 && effect.targetCel < celNames.size()) comboIndex = effect.targetCel + 1;
    targetCombo->setCurrentIndex(comboIndex);
    connect(targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, effectIndex](int index) { onEffectTargetChanged(effectIndex, index); });
    headerRow->addWidget(targetCombo, 1);

    auto* upButton = new QToolButton(box);
    upButton->setText(tr("上へ"));
    upButton->setEnabled(effectIndex > 0);
    connect(upButton, &QToolButton::clicked, this, [this, effectIndex] { moveEffect(effectIndex, -1); });
    headerRow->addWidget(upButton);

    auto* downButton = new QToolButton(box);
    downButton->setText(tr("下へ"));
    downButton->setEnabled(effectIndex + 1 < static_cast<int>(cut->effects().size()));
    connect(downButton, &QToolButton::clicked, this, [this, effectIndex] { moveEffect(effectIndex, +1); });
    headerRow->addWidget(downButton);

    auto* removeButton = new QToolButton(box);
    removeButton->setText(tr("削除"));
    connect(removeButton, &QToolButton::clicked, this, [this, effectIndex] { removeEffect(effectIndex); });
    headerRow->addWidget(removeButton);

    vlayout->addLayout(headerRow);

    // パラメータ行: [ストップウォッチ] [パラメータ名] [スピン] [◆キー有無/移動]
    const std::map<std::string, double> shown = effect.paramsAt(static_cast<size_t>(m_koma));
    for (const auto& [key, value] : shown) {
        auto* row = new QHBoxLayout();
        const std::string keyCopy = key;
        const bool hasCurve = effect.hasCurve(key);

        auto* stopwatch = new QToolButton(box);
        stopwatch->setCheckable(true);
        stopwatch->setChecked(hasCurve);
        stopwatch->setText(QStringLiteral("⏱"));  // ⏱
        stopwatch->setToolTip(tr("キーフレーム有効化"));
        connect(stopwatch, &QToolButton::toggled, this,
                [this, effectIndex, keyCopy](bool checked) { onStopwatchToggled(effectIndex, keyCopy, checked); });
        row->addWidget(stopwatch);

        row->addWidget(new QLabel(paramLabel(key), box));

        auto* spin = new QDoubleSpinBox(box);
        spin->setRange(0.0, isRgbParam(key) ? 255.0 : 4096.0);
        spin->setDecimals(2);
        spin->setSingleStep(isDensityParam(key) ? 0.05 : 1.0);
        spin->setValue(value);
        spin->setFocusPolicy(Qt::ClickFocus);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, effectIndex, keyCopy](double v) { onParamSpinChanged(effectIndex, keyCopy, v); });
        row->addWidget(spin, 1);

        auto* diamond = new QToolButton(box);
        const bool hasKeyHere = effect.hasKeyAt(key, static_cast<size_t>(m_koma));
        diamond->setText(hasKeyHere ? QStringLiteral("◆") : (hasCurve ? QStringLiteral("◇") : QString()));
        diamond->setEnabled(hasCurve);
        diamond->setToolTip(tr("現在コマのキーをトグル"));
        connect(diamond, &QToolButton::clicked, this,
                [this, effectIndex, keyCopy] { onKeyDiamondClicked(effectIndex, keyCopy); });
        row->addWidget(diamond);

        vlayout->addLayout(row);

        ParamRowWidgets rw;
        rw.effectIndex = effectIndex;
        rw.key = key;
        rw.stopwatch = stopwatch;
        rw.spin = spin;
        rw.diamond = diamond;
        m_paramRows.push_back(rw);
    }

    return box;
}

void ShootingWindow::rebuildEffectControls() {
    m_updating = true;
    m_paramRows.clear();

    // 既存のエフェクトGroupBoxを全て破棄する(「エフェクトを追加」ボタン以降は維持する)
    while (m_effectContainerLayout->count() > 0) {
        QLayoutItem* item = m_effectContainerLayout->itemAt(0);
        if (item->widget() == m_addEffectButton) break;
        m_effectContainerLayout->removeItem(item);
        delete item->widget();
        delete item;
    }

    core::Cut* cut = currentCut();
    if (cut) {
        QStringList celNames;
        for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));
        for (int i = 0; i < static_cast<int>(cut->effects().size()); ++i) {
            m_effectContainerLayout->insertWidget(i, buildEffectGroupBox(i, celNames));
        }
    }
    m_addEffectButton->setEnabled(cut != nullptr);
    m_updating = false;
}

void ShootingWindow::refreshParamRowValues() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    m_updating = true;
    for (const ParamRowWidgets& rw : m_paramRows) {
        if (rw.effectIndex < 0 || rw.effectIndex >= static_cast<int>(cut->effects().size())) continue;
        const core::Effect& effect = cut->effects()[static_cast<size_t>(rw.effectIndex)];
        const double fallback = effect.params.count(rw.key) ? effect.params.at(rw.key) : 0.0;
        if (rw.spin) rw.spin->setValue(effect.valueAt(rw.key, static_cast<size_t>(m_koma), fallback));
        if (rw.diamond) {
            const bool hasCurve = effect.hasCurve(rw.key);
            const bool hasKeyHere = effect.hasKeyAt(rw.key, static_cast<size_t>(m_koma));
            rw.diamond->setText(hasKeyHere ? QStringLiteral("◆")
                                            : (hasCurve ? QStringLiteral("◇") : QString()));
            rw.diamond->setEnabled(hasCurve);
        }
    }
    m_updating = false;
}

void ShootingWindow::rebuildTimeline() {
    m_updating = true;
    m_timelineRows.clear();
    core::Cut* cut = currentCut();
    if (!cut) {
        m_timeline->setRowCount(0);
        m_timeline->setColumnCount(0);
        m_updating = false;
        return;
    }

    QStringList celNames;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));

    QStringList rowLabels;
    for (int i = 0; i < static_cast<int>(cut->effects().size()); ++i) {
        const core::Effect& effect = cut->effects()[static_cast<size_t>(i)];
        // hasCurveなパラメータ(paramCurvesに空でない曲線を持つもの)だけを行にする
        for (const auto& [key, curve] : effect.paramCurves) {
            if (curve.empty()) continue;
            m_timelineRows.emplace_back(i, key);
            rowLabels.append(QStringLiteral("%1 / %2").arg(effectRowLabel(effect, celNames), paramLabel(key)));
        }
    }

    const int rows = static_cast<int>(m_timelineRows.size());
    const int cols = static_cast<int>(cut->frameCount());
    m_timeline->setRowCount(rows);
    m_timeline->setColumnCount(cols);
    m_timeline->setVerticalHeaderLabels(rowLabels);

    QStringList colLabels;
    for (int c = 0; c < cols; ++c) colLabels.append(QString::number(c + 1));
    m_timeline->setHorizontalHeaderLabels(colLabels);

    for (int r = 0; r < rows; ++r) {
        const core::Effect& effect = cut->effects()[static_cast<size_t>(m_timelineRows[static_cast<size_t>(r)].first)];
        const std::string& key = m_timelineRows[static_cast<size_t>(r)].second;
        for (int c = 0; c < cols; ++c) {
            const bool hasKey = effect.hasKeyAt(key, static_cast<size_t>(c));
            auto* item = new QTableWidgetItem(hasKey ? QStringLiteral("◆") : QString());
            item->setTextAlignment(Qt::AlignCenter);
            m_timeline->setItem(r, c, item);
        }
    }

    m_updating = false;
    refreshTimelineHighlight();
}

void ShootingWindow::refreshTimelineHighlight() {
    if (!m_timeline) return;
    const int cols = m_timeline->columnCount();
    const int rows = m_timeline->rowCount();
    for (int c = 0; c < cols; ++c) {
        const bool isCurrent = (c == m_koma);
        QTableWidgetItem* headerItem = m_timeline->horizontalHeaderItem(c);
        if (headerItem) {
            QFont f = headerItem->font();
            f.setBold(isCurrent);
            headerItem->setFont(f);
            headerItem->setBackground(isCurrent ? QBrush(kCtiHeaderColor) : QBrush());
        }
        for (int r = 0; r < rows; ++r) {
            QTableWidgetItem* item = m_timeline->item(r, c);
            if (!item) continue;
            item->setBackground(isCurrent ? QBrush(kCtiColumnColor) : QBrush());
        }
    }
}

void ShootingWindow::updatePreview() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_previewLabel->clear();
        return;
    }
    const core::Bitmap bitmap = core::renderCutFrame(*cut, static_cast<size_t>(m_koma), m_canvasWidth, m_canvasHeight);
    const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    QSize target = m_previewLabel->size();
    if (target.width() < 64 || target.height() < 64) target = QSize(640, 360);
    // scaled()は新しいQImageを作る(元のピクセルへの参照が切れる)ため、bitmapの寿命外でも安全
    m_previewLabel->setPixmap(
        QPixmap::fromImage(image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void ShootingWindow::updateTransportLabel() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_komaLabel->clear();
        return;
    }
    const double seconds = static_cast<double>(m_koma) / 24.0;
    m_komaLabel->setText(
        tr("コマ %1 / %2 (%3s)").arg(m_koma + 1).arg(cut->frameCount()).arg(seconds, 0, 'f', 2));
}

void ShootingWindow::setKoma(int koma) {
    core::Cut* cut = currentCut();
    const int maxKoma = cut ? static_cast<int>(cut->frameCount()) - 1 : 0;
    m_koma = std::clamp(koma, 0, std::max(0, maxKoma));
    refreshParamRowValues();
    refreshTimelineHighlight();
    updatePreview();
    updateTransportLabel();
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
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::removeEffect(int effectIndex) {
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects().erase(cut->effects().begin() + effectIndex);
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::moveEffect(int effectIndex, int delta) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    const int newIndex = effectIndex + delta;
    if (effectIndex < 0 || newIndex < 0 || newIndex >= static_cast<int>(cut->effects().size())) return;
    std::swap(cut->effects()[static_cast<size_t>(effectIndex)], cut->effects()[static_cast<size_t>(newIndex)]);
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::onEffectEnabledChanged(int effectIndex, bool enabled) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].enabled = enabled;
    markEdited();
}

void ShootingWindow::onEffectTargetChanged(int effectIndex, int comboIndex) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].targetCel = comboIndex <= 0 ? -1 : comboIndex - 1;
    rebuildEffectControls();  // タイトルの対象表示が変わるので作り直す
    rebuildTimeline();        // タイムライン行ラベルの対象表示も追従させる
    markEdited();
}

void ShootingWindow::onStopwatchToggled(int effectIndex, const std::string& key, bool checked) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (checked) {
        // 現在コマに現在値でキーを1個打つ(キーフレーム化)
        const double fallback = effect.params.count(key) ? effect.params.at(key) : 0.0;
        const double current = effect.valueAt(key, static_cast<size_t>(m_koma), fallback);
        effect.setKey(key, static_cast<size_t>(m_koma), current);
    } else {
        // 全キーを消す(以後は基本値=paramsを使う)
        effect.paramCurves[key].clear();
    }
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::onParamSpinChanged(int effectIndex, const std::string& key, double value) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (effect.hasCurve(key)) {
        // AE同様、キー持ちパラメータはスピン編集のたびに現在コマへ自動でキーを打つ
        effect.setKey(key, static_cast<size_t>(m_koma), value);
    } else {
        effect.params[key] = value;
    }

    // 対応する◆ボタンだけを軽量更新する(パネル全体は作り直さず、編集中のフォーカスを保つ)
    for (const ParamRowWidgets& rw : m_paramRows) {
        if (rw.effectIndex == effectIndex && rw.key == key && rw.diamond) {
            const bool hasCurve = effect.hasCurve(key);
            const bool hasKeyHere = effect.hasKeyAt(key, static_cast<size_t>(m_koma));
            rw.diamond->setText(hasKeyHere ? QStringLiteral("◆")
                                            : (hasCurve ? QStringLiteral("◇") : QString()));
            rw.diamond->setEnabled(hasCurve);
            break;
        }
    }

    rebuildTimeline();
    markEdited();
}

void ShootingWindow::onKeyDiamondClicked(int effectIndex, const std::string& key) {
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (!effect.hasCurve(key)) return;  // キーフレーム化されていないパラメータは対象外
    const size_t frame = static_cast<size_t>(m_koma);
    if (effect.hasKeyAt(key, frame)) {
        effect.removeKey(key, frame);  // 最後の1個ならストップウォッチも自動でOFFになる(hasCurve==false)
    } else {
        effect.setKey(key, frame, effect.valueAt(key, frame));
    }
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::onTimelineCellClicked(int row, int column) {
    if (m_updating || column < 0) return;
    Q_UNUSED(row);
    setKoma(column);
}

void ShootingWindow::onTimelineCellDoubleClicked(int row, int column) {
    if (row < 0 || row >= static_cast<int>(m_timelineRows.size()) || column < 0) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    const auto [effectIndex, key] = m_timelineRows[static_cast<size_t>(row)];
    if (effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    const size_t frame = static_cast<size_t>(column);

    setKoma(column);  // ダブルクリックでもCTIを移動させておく(見た目の一貫性)
    if (effect.hasKeyAt(key, frame)) {
        effect.removeKey(key, frame);
    } else {
        effect.setKey(key, frame, effect.valueAt(key, frame));
    }
    rebuildEffectControls();
    rebuildTimeline();
    markEdited();
}

void ShootingWindow::onTimelineHeaderClicked(int column) {
    if (m_updating || column < 0) return;
    setKoma(column);
}

void ShootingWindow::togglePlayback() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    if (m_playTimer->isActive()) {
        m_playTimer->stop();
        m_playButton->setText(tr("再生"));
    } else {
        m_playTimer->start();
        m_playButton->setText(tr("一時停止"));
    }
}

void ShootingWindow::onPlaybackTick() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_playTimer->stop();
        return;
    }
    int next = m_koma + 1;
    if (next >= static_cast<int>(cut->frameCount())) next = 0;  // 末尾でループ
    setKoma(next);
}

void ShootingWindow::debugSelectKoma(int koma) { setKoma(koma); }

void ShootingWindow::markEdited() {
    updatePreview();
    emit edited();
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
            markEdited();
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
            markEdited();
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
            markEdited();
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
    markEdited();
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
    markEdited();
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
    markEdited();
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
    markEdited();
}
