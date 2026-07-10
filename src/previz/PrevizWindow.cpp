#include "PrevizWindow.h"

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "previz/PrevizSheetPanel.h"
#include "previz/PrevizViewport.h"

PrevizWindow::PrevizWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("プリビズ - perapera-anime-maker901"));
    resize(960, 600);

    m_viewport = new PrevizViewport(this);
    setCentralWidget(m_viewport);
    connect(m_viewport, &PrevizViewport::cameraEdited, this, [this] {
        refreshCameraUi();
        emit sceneEdited();
    });
    connect(m_viewport, &PrevizViewport::modelEdited, this, [this] {
        refreshTransformUi();
        emit sceneEdited();
    });

    // カメラ設定ツールバー(物理カメラ: 焦点距離→画角)
    QToolBar* toolBar = addToolBar(tr("カメラ"));
    toolBar->setMovable(false);

    // 視点トグル(MMD/Blender方式): 既定は作業視点、チェックでカメラ視点を覗く
    QAction* cameraViewAction = toolBar->addAction(tr("カメラ視点"));
    cameraViewAction->setCheckable(true);
    connect(cameraViewAction, &QAction::toggled, this, [this](bool checked) {
        m_viewport->setViewMode(checked ? PrevizViewport::ViewMode::Camera : PrevizViewport::ViewMode::Work);
        statusBar()->showMessage(checked ? tr("カメラ視点: 右ドラッグ=見回し / 中=移動 / ホイール=前後(カメラを編集)")
                                         : tr("作業視点: 右ドラッグ=軌道 / 中=パン / ホイール=距離 / 左ドラッグ=選択モデル移動(Shift=上下)"));
    });
    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(tr(" 焦点距離: "), this));
    m_focalSpin = new QDoubleSpinBox(this);
    m_focalSpin->setRange(8.0, 300.0);
    m_focalSpin->setValue(50.0);
    m_focalSpin->setSuffix(tr(" mm"));
    m_focalSpin->setFocusPolicy(Qt::ClickFocus);
    connect(m_focalSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_updating || !m_scene) return;
        m_scene->camera.state.focalLengthMm = static_cast<float>(value);
        refreshCameraUi();
        m_viewport->update();
        emit sceneEdited();
    });
    toolBar->addWidget(m_focalSpin);
    m_fovLabel = new QLabel(this);
    toolBar->addWidget(m_fovLabel);

    // モデル一覧ドック
    auto* dock = new QDockWidget(tr("モデル"), this);
    dock->setObjectName(QStringLiteral("PrevizModelDock"));
    auto* container = new QWidget(dock);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    m_modelList = new QListWidget(container);
    layout->addWidget(m_modelList);
    auto* addButton = new QPushButton(tr("モデル追加..."), container);
    layout->addWidget(addButton);
    auto* removeButton = new QPushButton(tr("モデル削除"), container);
    layout->addWidget(removeButton);
    dock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(addButton, &QPushButton::clicked, this, &PrevizWindow::addModel);
    connect(removeButton, &QPushButton::clicked, this, &PrevizWindow::removeSelectedModel);
    connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_viewport->setSelectedModel(row);  // 作業視点の左ドラッグ移動対象
        refreshTransformUi();
        rebuildSheet();  // アクティブ列(選択モデル)の表示を追従させる
    });

    // 選択モデルの配置編集
    auto* transformLabel = new QLabel(tr("配置(選択モデル)"), container);
    layout->addWidget(transformLabel);
    const auto makeSpin = [container](double min, double max, double step) {
        auto* spin = new QDoubleSpinBox(container);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setFocusPolicy(Qt::ClickFocus);
        return spin;
    };
    m_posX = makeSpin(-1000, 1000, 0.1);
    m_posY = makeSpin(-1000, 1000, 0.1);
    m_posZ = makeSpin(-1000, 1000, 0.1);
    m_rotY = makeSpin(-3600, 3600, 5.0);
    m_scale = makeSpin(0.01, 100, 0.1);
    const auto addRow = [container, layout](const QString& label, QWidget* w) {
        auto* row = new QWidget(container);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(4, 0, 4, 0);
        h->addWidget(new QLabel(label, row));
        h->addWidget(w, 1);
        layout->addWidget(row);
    };
    addRow(tr("X"), m_posX);
    addRow(tr("Y"), m_posY);
    addRow(tr("Z"), m_posZ);
    addRow(tr("回転Y°"), m_rotY);
    addRow(tr("倍率"), m_scale);
    for (QDoubleSpinBox* spin : {m_posX, m_posY, m_posZ, m_rotY, m_scale}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { applyTransformFromUi(); });
    }

    // モーションキー(カメラ/選択モデル): 現在コマにキーを打つ・消す
    auto* cameraKeyButton = new QPushButton(tr("現在コマにカメラキー"), container);
    layout->addWidget(cameraKeyButton);
    connect(cameraKeyButton, &QPushButton::clicked, this, [this] {
        if (!m_scene) return;
        m_scene->camera.keys[m_viewport->frame()] = m_scene->camera.stateAt(m_viewport->frame());
        rebuildSheet();
        emit sceneEdited();
    });
    auto* cameraKeyClearButton = new QPushButton(tr("カメラキー削除"), container);
    layout->addWidget(cameraKeyClearButton);
    connect(cameraKeyClearButton, &QPushButton::clicked, this, [this] {
        if (!m_scene) return;
        m_scene->camera.keys.erase(m_viewport->frame());
        m_viewport->update();
        rebuildSheet();
        emit sceneEdited();
    });
    auto* modelKeyButton = new QPushButton(tr("現在コマにモデルキー"), container);
    layout->addWidget(modelKeyButton);
    connect(modelKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model) return;
        model->transformKeys[m_viewport->frame()] = model->transformAt(m_viewport->frame());
        rebuildSheet();
        emit sceneEdited();
    });

    // プリビズシート(下部ドック): 行=コマ、列=カメラ+モデル
    m_sheetPanel = new PrevizSheetPanel(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_sheetPanel);
    connect(m_sheetPanel, &PrevizSheetPanel::cellClicked, this, &PrevizWindow::onSheetCellClicked);
    connect(m_sheetPanel, &PrevizSheetPanel::keyToggleRequested, this, &PrevizWindow::onSheetKeyToggleRequested);

    // 十字リモコン(ナッジ操作)ドック。モデルドックの下に配置する
    auto* nudgeDock = new QDockWidget(tr("操作"), this);
    nudgeDock->setObjectName(QStringLiteral("PrevizNudgeDock"));
    auto* nudgeContainer = new QWidget(nudgeDock);
    auto* nudgeLayout = new QVBoxLayout(nudgeContainer);

    auto* targetRow = new QWidget(nudgeContainer);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->addWidget(new QLabel(tr("対象:"), targetRow));
    m_nudgeTargetCombo = new QComboBox(targetRow);
    m_nudgeTargetCombo->addItem(tr("カメラ"));
    m_nudgeTargetCombo->addItem(tr("選択モデル"));
    targetLayout->addWidget(m_nudgeTargetCombo, 1);
    nudgeLayout->addWidget(targetRow);

    const auto addStepRow = [nudgeContainer, nudgeLayout](const QString& label, QDoubleSpinBox* spin) {
        auto* row = new QWidget(nudgeContainer);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(label, row));
        h->addWidget(spin, 1);
        nudgeLayout->addWidget(row);
    };
    m_moveStepSpin = new QDoubleSpinBox(nudgeContainer);
    m_moveStepSpin->setRange(0.01, 10.0);
    m_moveStepSpin->setValue(0.5);
    m_moveStepSpin->setSuffix(tr(" m"));
    m_moveStepSpin->setFocusPolicy(Qt::ClickFocus);
    addStepRow(tr("移動ステップ:"), m_moveStepSpin);
    m_rotStepSpin = new QDoubleSpinBox(nudgeContainer);
    m_rotStepSpin->setRange(1.0, 90.0);
    m_rotStepSpin->setValue(5.0);
    m_rotStepSpin->setSuffix(tr("°"));
    m_rotStepSpin->setFocusPolicy(Qt::ClickFocus);
    addStepRow(tr("回転ステップ:"), m_rotStepSpin);

    // ボタングリッド: 上段=前後左右(床面移動)、中段=上下、下段=ヨー回転、最下段=ピッチ(カメラのみ)
    auto* grid = new QGridLayout();
    const auto makeNudgeButton = [nudgeContainer](const QString& text, const QString& tooltip) {
        auto* button = new QToolButton(nudgeContainer);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAutoRepeat(true);
        return button;
    };
    auto* zMinusButton = makeNudgeButton(tr("↑"), tr("奥へ移動(Z-)"));
    auto* xMinusButton = makeNudgeButton(tr("←"), tr("左へ移動(X-)"));
    auto* xPlusButton = makeNudgeButton(tr("→"), tr("右へ移動(X+)"));
    auto* zPlusButton = makeNudgeButton(tr("↓"), tr("手前へ移動(Z+)"));
    auto* yPlusButton = makeNudgeButton(tr("上"), tr("上へ移動(Y+)"));
    auto* yMinusButton = makeNudgeButton(tr("下"), tr("下へ移動(Y-)"));
    auto* yawLeftButton = makeNudgeButton(tr("回転←"), tr("左回転(ヨー+)"));
    auto* yawRightButton = makeNudgeButton(tr("回転→"), tr("右回転(ヨー-)"));
    m_pitchUpButton = makeNudgeButton(tr("ピッチ↑"), tr("見上げる(ピッチ+、カメラのみ)"));
    m_pitchDownButton = makeNudgeButton(tr("ピッチ↓"), tr("見下ろす(ピッチ-、カメラのみ)"));
    grid->addWidget(zMinusButton, 0, 1);
    grid->addWidget(xMinusButton, 1, 0);
    grid->addWidget(xPlusButton, 1, 2);
    grid->addWidget(zPlusButton, 2, 1);
    grid->addWidget(yPlusButton, 3, 0);
    grid->addWidget(yMinusButton, 3, 2);
    grid->addWidget(yawLeftButton, 4, 0);
    grid->addWidget(yawRightButton, 4, 2);
    grid->addWidget(m_pitchUpButton, 5, 0);
    grid->addWidget(m_pitchDownButton, 5, 2);
    nudgeLayout->addLayout(grid);
    nudgeLayout->addStretch();

    nudgeDock->setWidget(nudgeContainer);
    addDockWidget(Qt::RightDockWidgetArea, nudgeDock);
    splitDockWidget(dock, nudgeDock, Qt::Vertical);  // モデルドックの下に配置

    connect(m_nudgeTargetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool isCamera = (index == 0);
        m_pitchUpButton->setEnabled(isCamera);
        m_pitchDownButton->setEnabled(isCamera);
    });
    connect(zMinusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.z -= step; },
                   [step](core::PrevizTransform& t) { t.position.z -= step; });
    });
    connect(zPlusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.z += step; },
                   [step](core::PrevizTransform& t) { t.position.z += step; });
    });
    connect(xMinusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.x -= step; },
                   [step](core::PrevizTransform& t) { t.position.x -= step; });
    });
    connect(xPlusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.x += step; },
                   [step](core::PrevizTransform& t) { t.position.x += step; });
    });
    connect(yPlusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.y += step; },
                   [step](core::PrevizTransform& t) { t.position.y += step; });
    });
    connect(yMinusButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_moveStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.position.y -= step; },
                   [step](core::PrevizTransform& t) { t.position.y -= step; });
    });
    connect(yawLeftButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_rotStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.rotationDeg.y += step; },
                   [step](core::PrevizTransform& t) { t.rotationDeg.y += step; });
    });
    connect(yawRightButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_rotStepSpin->value());
        applyNudge([step](core::PrevizCameraState& s) { s.rotationDeg.y -= step; },
                   [step](core::PrevizTransform& t) { t.rotationDeg.y -= step; });
    });
    connect(m_pitchUpButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_rotStepSpin->value());
        applyNudge(
            [step](core::PrevizCameraState& s) {
                s.rotationDeg.x = std::clamp(s.rotationDeg.x + step, -89.0f, 89.0f);
            },
            [](core::PrevizTransform&) {});  // モデルには無効(ピッチボタンはカメラ選択時のみ有効)
    });
    connect(m_pitchDownButton, &QToolButton::clicked, this, [this] {
        const float step = static_cast<float>(m_rotStepSpin->value());
        applyNudge(
            [step](core::PrevizCameraState& s) {
                s.rotationDeg.x = std::clamp(s.rotationDeg.x - step, -89.0f, 89.0f);
            },
            [](core::PrevizTransform&) {});
    });
    m_pitchUpButton->setEnabled(true);   // 初期対象は「カメラ」
    m_pitchDownButton->setEnabled(true);

    statusBar()->showMessage(
        tr("作業視点: 右ドラッグ=軌道 / 中=パン / ホイール=距離 / 左ドラッグ=選択モデル移動(Shift=上下)"));
}

core::PrevizModel* PrevizWindow::selectedModel() {
    if (!m_scene) return nullptr;
    const int row = m_modelList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_scene->models.size())) return nullptr;
    return &m_scene->models[static_cast<size_t>(row)];
}

void PrevizWindow::refreshTransformUi() {
    core::PrevizModel* model = selectedModel();
    if (!model) return;
    m_updating = true;
    const core::PrevizTransform tf = model->transformAt(m_viewport->frame());
    m_posX->setValue(tf.position.x);
    m_posY->setValue(tf.position.y);
    m_posZ->setValue(tf.position.z);
    m_rotY->setValue(tf.rotationDeg.y);
    m_scale->setValue(tf.scale.x);
    m_updating = false;
}

void PrevizWindow::applyTransformFromUi() {
    if (m_updating) return;
    core::PrevizModel* model = selectedModel();
    if (!model) return;

    core::PrevizTransform tf = model->transformAt(m_viewport->frame());
    tf.position = {static_cast<float>(m_posX->value()), static_cast<float>(m_posY->value()),
                   static_cast<float>(m_posZ->value())};
    tf.rotationDeg.y = static_cast<float>(m_rotY->value());
    const float s = static_cast<float>(m_scale->value());
    tf.scale = {s, s, s};

    // キーが無ければ基本配置、キーがあれば現在コマのキーを編集(カメラと同じ規則)
    if (model->transformKeys.empty()) {
        model->transform = tf;
    } else {
        model->transformKeys[m_viewport->frame()] = tf;
    }
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::setScene(core::PrevizScene* scene) {
    m_scene = scene;
    m_viewport->setScene(scene);
    refreshModelList();
    refreshCameraUi();
    rebuildSheet();
}

void PrevizWindow::setFrame(size_t frame) {
    setTimeline(frame, m_frameCount);
}

void PrevizWindow::setTimeline(size_t currentFrame, size_t frameCount) {
    m_frameCount = frameCount > 0 ? frameCount : 1;
    m_viewport->setFrame(currentFrame);
    refreshCameraUi();
    refreshTransformUi();  // モーションキーがあるとコマごとに配置が変わる
    rebuildSheet();
}

void PrevizWindow::addModel() {
    if (!m_scene) return;
    const QString path =
        QFileDialog::getOpenFileName(this, tr("3Dモデルを開く"), QString(), tr("glTFモデル (*.glb *.gltf)"));
    if (path.isEmpty()) return;

    core::PrevizModel model;
    model.name = QFileInfo(path).completeBaseName().toStdString();
    model.filePath = path.toStdString();
    m_scene->models.push_back(std::move(model));

    refreshModelList();
    m_viewport->update();
    rebuildSheet();
    emit sceneEdited();
}

void PrevizWindow::removeSelectedModel() {
    if (!m_scene) return;
    const int row = m_modelList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_scene->models.size())) return;
    m_scene->models.erase(m_scene->models.begin() + row);
    refreshModelList();
    m_viewport->update();
    rebuildSheet();
    emit sceneEdited();
}

void PrevizWindow::refreshModelList() {
    m_updating = true;
    m_modelList->clear();
    if (m_scene) {
        for (const core::PrevizModel& model : m_scene->models) {
            m_modelList->addItem(QString::fromStdString(model.name));
        }
    }
    m_updating = false;
}

void PrevizWindow::refreshCameraUi() {
    if (!m_scene) return;
    m_updating = true;
    m_focalSpin->setValue(m_scene->camera.stateAt(m_viewport->frame()).focalLengthMm);
    m_fovLabel->setText(tr(" 水平画角: %1°").arg(m_scene->camera.horizontalFovDeg(m_viewport->frame()), 0, 'f', 1));
    m_updating = false;
}

// プリビズシートの列(カメラ+モデル)とキー有無を集めて反映する
void PrevizWindow::rebuildSheet() {
    if (!m_sheetPanel) return;
    QStringList columnNames;
    columnNames << tr("カメラ");
    QList<QList<bool>> keyFlags;

    const int frameCount = static_cast<int>(m_frameCount);
    QList<bool> cameraFlags;
    cameraFlags.reserve(frameCount);
    for (int f = 0; f < frameCount; ++f) {
        cameraFlags << (m_scene && m_scene->camera.keys.count(static_cast<size_t>(f)) > 0);
    }
    keyFlags << cameraFlags;

    if (m_scene) {
        for (const core::PrevizModel& model : m_scene->models) {
            columnNames << QString::fromStdString(model.name);
            QList<bool> flags;
            flags.reserve(frameCount);
            for (int f = 0; f < frameCount; ++f) {
                flags << (model.transformKeys.count(static_cast<size_t>(f)) > 0);
            }
            keyFlags << flags;
        }
    }

    // モデル選択中ならその列(1+行)、そうでなければカメラ列(0)をアクティブにする
    const int row = m_modelList ? m_modelList->currentRow() : -1;
    const int activeColumn = row >= 0 ? 1 + row : 0;

    m_sheetPanel->setSheet(columnNames, keyFlags, frameCount, static_cast<int>(m_viewport->frame()), activeColumn);
}

void PrevizWindow::onSheetCellClicked(int column, int frame) {
    emit frameChangeRequested(frame);
    if (column > 0 && m_modelList) {
        m_modelList->setCurrentRow(column - 1);  // モデル選択と連動
    }
}

void PrevizWindow::onSheetKeyToggleRequested(int column, int frame) {
    if (!m_scene) return;
    const size_t f = static_cast<size_t>(frame);
    if (column == 0) {
        core::PrevizCamera& camera = m_scene->camera;
        if (camera.keys.count(f) > 0) {
            camera.keys.erase(f);
        } else {
            camera.keys[f] = camera.stateAt(f);
        }
    } else {
        const int row = column - 1;
        if (row < 0 || row >= static_cast<int>(m_scene->models.size())) return;
        core::PrevizModel& model = m_scene->models[static_cast<size_t>(row)];
        if (model.transformKeys.count(f) > 0) {
            model.transformKeys.erase(f);
        } else {
            model.transformKeys[f] = model.transformAt(f);
        }
    }
    rebuildSheet();
    m_viewport->update();
    refreshCameraUi();
    refreshTransformUi();
    emit sceneEdited();
}

// キー規則: キーが無ければ基本状態、あれば現在コマのキーを編集する(PrevizViewportの操作と同じ規則)
core::PrevizCameraState& PrevizWindow::editableCamera() {
    core::PrevizCamera& camera = m_scene->camera;
    if (camera.keys.empty()) return camera.state;
    camera.keys[m_viewport->frame()] = camera.stateAt(m_viewport->frame());  // 補間値を起点にキー化
    return camera.keys[m_viewport->frame()];
}

core::PrevizTransform& PrevizWindow::editableModelTransform(core::PrevizModel& model) {
    if (model.transformKeys.empty()) return model.transform;
    model.transformKeys[m_viewport->frame()] = model.transformAt(m_viewport->frame());
    return model.transformKeys[m_viewport->frame()];
}

// 十字リモコンのボタン押下を、対象(カメラ/選択モデル)に応じて適用する
void PrevizWindow::applyNudge(const std::function<void(core::PrevizCameraState&)>& cameraFn,
                               const std::function<void(core::PrevizTransform&)>& modelFn) {
    if (!m_scene) return;
    if (m_nudgeTargetCombo->currentIndex() == 0) {
        cameraFn(editableCamera());
    } else {
        core::PrevizModel* model = selectedModel();
        if (!model) return;
        modelFn(editableModelTransform(*model));
    }
    m_viewport->update();
    refreshTransformUi();
    refreshCameraUi();
    rebuildSheet();
    emit sceneEdited();
}
