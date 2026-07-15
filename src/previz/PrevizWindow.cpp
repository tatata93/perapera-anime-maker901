#include "PrevizWindow.h"

#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "previz/PrevizSheetPanel.h"
#include "previz/PrevizViewport.h"

namespace {

bool isHumanoidKind(const std::string& filePath) {
    return filePath == ":humanoid" || filePath == ":humanoid_box";
}

core::PrevizHumanoidPose humanoidPosePreset(int presetIndex) {
    core::PrevizHumanoidPose pose;
    switch (presetIndex) {
        case 1:  // walk A
            pose.torsoPitchDeg = 4.0f;
            pose.torsoRollDeg = -2.0f;
            pose.headYawDeg = -4.0f;
            pose.leftShoulderPitchDeg = 28.0f;
            pose.leftShoulderRollDeg = -4.0f;
            pose.leftElbowDeg = 12.0f;
            pose.rightShoulderPitchDeg = -32.0f;
            pose.rightShoulderRollDeg = 4.0f;
            pose.rightElbowDeg = 18.0f;
            pose.leftHipPitchDeg = -28.0f;
            pose.leftHipRollDeg = -3.0f;
            pose.leftKneeDeg = 18.0f;
            pose.rightHipPitchDeg = 32.0f;
            pose.rightHipRollDeg = 3.0f;
            pose.rightKneeDeg = 34.0f;
            break;
        case 2:  // walk B
            pose.torsoPitchDeg = 4.0f;
            pose.torsoRollDeg = 2.0f;
            pose.headYawDeg = 4.0f;
            pose.leftShoulderPitchDeg = -32.0f;
            pose.leftShoulderRollDeg = -4.0f;
            pose.leftElbowDeg = 18.0f;
            pose.rightShoulderPitchDeg = 28.0f;
            pose.rightShoulderRollDeg = 4.0f;
            pose.rightElbowDeg = 12.0f;
            pose.leftHipPitchDeg = 32.0f;
            pose.leftHipRollDeg = -3.0f;
            pose.leftKneeDeg = 34.0f;
            pose.rightHipPitchDeg = -28.0f;
            pose.rightHipRollDeg = 3.0f;
            pose.rightKneeDeg = 18.0f;
            break;
        case 3:  // right arm up
            pose.torsoPitchDeg = 6.0f;
            pose.headYawDeg = 12.0f;
            pose.leftShoulderPitchDeg = 12.0f;
            pose.leftElbowDeg = 18.0f;
            pose.rightShoulderPitchDeg = -145.0f;
            pose.rightShoulderRollDeg = -12.0f;
            pose.rightElbowDeg = -18.0f;
            pose.leftHipPitchDeg = 6.0f;
            pose.rightHipPitchDeg = -8.0f;
            break;
        case 4:  // crouch
            pose.torsoPitchDeg = 20.0f;
            pose.leftShoulderPitchDeg = 18.0f;
            pose.leftElbowDeg = 32.0f;
            pose.rightShoulderPitchDeg = 18.0f;
            pose.rightElbowDeg = 32.0f;
            pose.leftHipPitchDeg = 42.0f;
            pose.leftKneeDeg = 68.0f;
            pose.rightHipPitchDeg = 42.0f;
            pose.rightKneeDeg = 68.0f;
            break;
        case 5:  // open limbs
            pose.torsoPitchDeg = 2.0f;
            pose.leftShoulderRollDeg = -55.0f;
            pose.leftElbowDeg = 10.0f;
            pose.rightShoulderRollDeg = 55.0f;
            pose.rightElbowDeg = 10.0f;
            pose.leftHipRollDeg = -28.0f;
            pose.leftKneeDeg = 6.0f;
            pose.rightHipRollDeg = 28.0f;
            pose.rightKneeDeg = 6.0f;
            break;
        case 0:
        default:
            break;
    }
    return pose;
}

core::PrevizHumanoidBody humanoidBodyPreset(int presetIndex) {
    core::PrevizHumanoidBody body;
    switch (presetIndex) {
        case 1:  // adult
            body.headScale = 0.92f;
            body.headHeight = 1.06f;
            body.torsoLength = 1.08f;
            body.chestHeight = 1.06f;
            body.chestWidth = 1.08f;
            body.bellyWidth = 0.98f;
            body.waistWidth = 0.92f;
            body.shoulderWidth = 1.12f;
            body.hipWidth = 0.98f;
            body.armLength = 1.08f;
            body.legLength = 1.12f;
            break;
        case 2:  // child
            body.headScale = 1.28f;
            body.headWidth = 1.05f;
            body.headDepth = 1.06f;
            body.torsoLength = 0.86f;
            body.bellyHeight = 1.08f;
            body.chestWidth = 0.82f;
            body.bellyWidth = 0.88f;
            body.waistWidth = 0.84f;
            body.shoulderWidth = 0.82f;
            body.hipWidth = 0.86f;
            body.armLength = 0.78f;
            body.armThickness = 0.82f;
            body.legLength = 0.76f;
            body.legThickness = 0.84f;
            body.handScale = 0.82f;
            body.footScale = 0.82f;
            break;
        case 3:  // sturdy
            body.headScale = 1.02f;
            body.headDepth = 1.08f;
            body.torsoLength = 1.02f;
            body.chestWidth = 1.28f;
            body.chestDepth = 1.18f;
            body.bellyWidth = 1.22f;
            body.bellyDepth = 1.15f;
            body.waistWidth = 1.18f;
            body.shoulderWidth = 1.18f;
            body.hipWidth = 1.16f;
            body.armLength = 0.96f;
            body.armThickness = 1.28f;
            body.armDepth = 1.18f;
            body.legLength = 0.96f;
            body.legThickness = 1.30f;
            body.legDepth = 1.18f;
            body.handScale = 1.16f;
            body.footScale = 1.14f;
            break;
        case 4:  // non-human
            body.headScale = 1.45f;
            body.headWidth = 1.18f;
            body.headHeight = 0.88f;
            body.headDepth = 1.22f;
            body.faceWidth = 1.30f;
            body.faceDepth = 1.45f;
            body.torsoLength = 0.92f;
            body.chestHeight = 0.88f;
            body.bellyHeight = 0.82f;
            body.chestWidth = 0.78f;
            body.chestDepth = 1.12f;
            body.bellyWidth = 0.70f;
            body.bellyDepth = 0.90f;
            body.waistWidth = 0.66f;
            body.shoulderWidth = 0.78f;
            body.hipWidth = 0.70f;
            body.armLength = 1.38f;
            body.armThickness = 0.62f;
            body.leftForearmLength = 1.18f;
            body.rightForearmLength = 1.18f;
            body.legLength = 1.16f;
            body.legThickness = 0.62f;
            body.handScale = 1.28f;
            body.footScale = 0.78f;
            break;
        case 0:
        default:
            break;
    }
    return body;
}

}  // namespace

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
        statusBar()->showMessage(checked ? tr("カメラ視点: 右ドラッグ=見回し / WASD+Q/E=移動 / ホイール=前後(カメラを編集)")
                                         : tr("作業視点: 右ドラッグ=軌道 / WASD+Q/E=移動 / 左ドラッグ=選択モデル移動(Shift=上下)"));
    });
    QAction* wireframeAction = toolBar->addAction(tr("ワイヤー"));
    wireframeAction->setCheckable(true);
    connect(wireframeAction, &QAction::toggled, this, [this](bool checked) {
        m_viewport->setWireframeEnabled(checked);
        statusBar()->showMessage(checked ? tr("ワイヤーフレーム表示") : tr("通常表示"));
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
        // キー規則: キーがあれば現在コマのキーの焦点距離を編集する(ズームのカメラワークも打てる)
        editableCamera().focalLengthMm = static_cast<float>(value);
        refreshCameraUi();
        rebuildSheet();  // キーが新規作成される場合がある
        m_viewport->update();
        emit sceneEdited();
    });
    toolBar->addWidget(m_focalSpin);
    m_fovLabel = new QLabel(this);
    toolBar->addWidget(m_fovLabel);

    // レンズ歪曲(魚眼/樽/糸巻き)。カメラ全体のレンズ特性(コマ非依存)。描画後の放射状ワープで表現する
    toolBar->addWidget(new QLabel(tr(" レンズ歪曲: "), this));
    m_distortSpin = new QDoubleSpinBox(this);
    m_distortSpin->setRange(-0.5, 1.5);
    m_distortSpin->setSingleStep(0.05);
    m_distortSpin->setValue(0.0);
    m_distortSpin->setToolTip(tr("0=標準(直線を直線に写す) / 正=樽型・魚眼(外へ膨らむ) / 負=糸巻き型"));
    m_distortSpin->setFocusPolicy(Qt::ClickFocus);
    connect(m_distortSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_updating || !m_scene) return;
        m_scene->camera.lensDistortion = static_cast<float>(value);
        m_viewport->update();
        emit sceneEdited();
    });
    toolBar->addWidget(m_distortSpin);

    // プリビズ内再生(モーション確認)
    toolBar->addSeparator();
    m_playAction = toolBar->addAction(tr("再生"));
    connect(m_playAction, &QAction::triggered, this, &PrevizWindow::togglePlayback);
    toolBar->addWidget(new QLabel(tr(" FPS: "), this));
    m_playFpsSpin = new QSpinBox(this);
    m_playFpsSpin->setRange(1, 60);
    m_playFpsSpin->setValue(24);
    m_playFpsSpin->setFocusPolicy(Qt::ClickFocus);
    connect(m_playFpsSpin, &QSpinBox::valueChanged, this, [this](int fps) {
        if (m_playing) m_playTimer->start(1000 / std::max(1, fps));
    });
    toolBar->addWidget(m_playFpsSpin);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        const size_t count = std::max<size_t>(1, m_frameCount);
        const size_t next = (m_viewport->frame() + 1) % count;
        m_viewport->setFrame(next);
        refreshCameraUi();
        refreshTransformUi();
        refreshBodyUi();
        refreshPoseUi();
        // シートの再構築は重いので再生中は行わない(停止時にまとめて更新)
    });

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

    // プリミティブ追加(箱/円柱/球)。任意の形へ変形できる下地として複数形状を用意する
    auto* addPrimitiveButton = new QToolButton(container);
    addPrimitiveButton->setText(tr("プリミティブ追加"));
    addPrimitiveButton->setPopupMode(QToolButton::InstantPopup);
    auto* primitiveMenu = new QMenu(addPrimitiveButton);
    QAction* addBoxAction = primitiveMenu->addAction(tr("箱"));
    QAction* addCylinderAction = primitiveMenu->addAction(tr("円柱"));
    QAction* addSphereAction = primitiveMenu->addAction(tr("球"));
    QAction* addHumanoidAction = primitiveMenu->addAction(tr("人型"));
    QAction* addRobotAction = primitiveMenu->addAction(tr("人型ロボット"));
    addPrimitiveButton->setMenu(primitiveMenu);
    layout->addWidget(addPrimitiveButton);
    connect(addBoxAction, &QAction::triggered, this, [this] { addPrimitive(QStringLiteral(":box"), true); });
    connect(addCylinderAction, &QAction::triggered, this, [this] { addPrimitive(QStringLiteral(":cylinder"), true); });
    connect(addSphereAction, &QAction::triggered, this, [this] { addPrimitive(QStringLiteral(":sphere"), true); });
    connect(addHumanoidAction, &QAction::triggered, this, [this] { addPrimitive(QStringLiteral(":humanoid"), true); });
    connect(addRobotAction, &QAction::triggered, this, [this] { addPrimitive(QStringLiteral(":humanoid_box"), true); });

    auto* removeButton = new QPushButton(tr("モデル削除"), container);
    layout->addWidget(removeButton);
    dock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(addButton, &QPushButton::clicked, this, &PrevizWindow::addModel);
    connect(removeButton, &QPushButton::clicked, this, &PrevizWindow::removeSelectedModel);
    connect(m_modelList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_viewport->setSelectedModel(row);  // 作業視点の左ドラッグ移動対象
        refreshTransformUi();
        refreshPoseUi();
        refreshBodyUi();
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
    m_rotX = makeSpin(-180, 180, 5.0);
    m_rotY = makeSpin(-180, 180, 5.0);
    m_rotZ = makeSpin(-180, 180, 5.0);
    // 非一様スケール(X/Y/Z個別)で箱→板/柱、球→楕円体、円柱→円盤/筒に変形できる
    m_scaleX = makeSpin(0.01, 100, 0.1);
    m_scaleY = makeSpin(0.01, 100, 0.1);
    m_scaleZ = makeSpin(0.01, 100, 0.1);
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
    addRow(tr("回転X°"), m_rotX);
    addRow(tr("回転Y°"), m_rotY);
    addRow(tr("回転Z°"), m_rotZ);
    addRow(tr("倍率X"), m_scaleX);
    addRow(tr("倍率Y"), m_scaleY);
    addRow(tr("倍率Z"), m_scaleZ);
    for (QDoubleSpinBox* spin : {m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ, m_scaleX, m_scaleY, m_scaleZ}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { applyTransformFromUi(); });
    }

    m_openPoseWindowButton = new QPushButton(tr("人型ポーズ..."), container);
    m_openBodyWindowButton = new QPushButton(tr("人型体型..."), container);
    layout->addWidget(m_openPoseWindowButton);
    layout->addWidget(m_openBodyWindowButton);
    connect(m_openPoseWindowButton, &QPushButton::clicked, this, &PrevizWindow::openPoseWindow);
    connect(m_openBodyWindowButton, &QPushButton::clicked, this, &PrevizWindow::openBodyWindow);

    const auto addPanelRow = [](QWidget* parent, QVBoxLayout* targetLayout, const QString& label, QWidget* w) {
        auto* row = new QWidget(parent);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(4, 0, 4, 0);
        h->addWidget(new QLabel(label, row));
        h->addWidget(w, 1);
        targetLayout->addWidget(row);
    };

    m_posePanel = new QWidget(this);
    auto* poseLayout = new QVBoxLayout(m_posePanel);
    poseLayout->setContentsMargins(8, 8, 8, 8);
    poseLayout->setSpacing(6);
    auto* poseLabel = new QLabel(tr("人型ポーズ"), m_posePanel);
    m_poseLabelWidget = poseLabel;
    poseLayout->addWidget(poseLabel);
    auto* posePresetRow = new QWidget(m_posePanel);
    m_posePresetRow = posePresetRow;
    auto* posePresetLayout = new QHBoxLayout(posePresetRow);
    posePresetLayout->setContentsMargins(4, 0, 4, 0);
    m_posePresetCombo = new QComboBox(posePresetRow);
    m_posePresetCombo->addItems({tr("ニュートラル"), tr("歩きA"), tr("歩きB"), tr("右腕上げ"), tr("しゃがみ"),
                                 tr("手足を開く")});
    auto* applyPosePresetButton = new QPushButton(tr("適用"), posePresetRow);
    posePresetLayout->addWidget(m_posePresetCombo, 1);
    posePresetLayout->addWidget(applyPosePresetButton);
    poseLayout->addWidget(posePresetRow);
    connect(applyPosePresetButton, &QPushButton::clicked, this, [this] {
        applyHumanoidPosePreset(m_posePresetCombo ? m_posePresetCombo->currentIndex() : 0);
    });

    const auto makePoseSpin = [this]() {
        auto* spin = new QDoubleSpinBox(m_posePanel);
        spin->setRange(-180.0, 180.0);
        spin->setSingleStep(5.0);
        spin->setDecimals(1);
        spin->setSuffix(QObject::tr("°"));
        spin->setFocusPolicy(Qt::ClickFocus);
        return spin;
    };
    m_poseTorsoPitch = makePoseSpin();
    m_poseTorsoRoll = makePoseSpin();
    m_poseHeadPitch = makePoseSpin();
    m_poseHeadYaw = makePoseSpin();
    m_poseLeftShoulder = makePoseSpin();
    m_poseLeftShoulderRoll = makePoseSpin();
    m_poseLeftElbow = makePoseSpin();
    m_poseRightShoulder = makePoseSpin();
    m_poseRightShoulderRoll = makePoseSpin();
    m_poseRightElbow = makePoseSpin();
    m_poseLeftHip = makePoseSpin();
    m_poseLeftHipRoll = makePoseSpin();
    m_poseLeftKnee = makePoseSpin();
    m_poseRightHip = makePoseSpin();
    m_poseRightHipRoll = makePoseSpin();
    m_poseRightKnee = makePoseSpin();
    addPanelRow(m_posePanel, poseLayout, tr("胴 前後"), m_poseTorsoPitch);
    addPanelRow(m_posePanel, poseLayout, tr("胴 左右傾き"), m_poseTorsoRoll);
    addPanelRow(m_posePanel, poseLayout, tr("頭 上下"), m_poseHeadPitch);
    addPanelRow(m_posePanel, poseLayout, tr("頭 左右"), m_poseHeadYaw);
    addPanelRow(m_posePanel, poseLayout, tr("左肩 前後"), m_poseLeftShoulder);
    addPanelRow(m_posePanel, poseLayout, tr("左肩 開き"), m_poseLeftShoulderRoll);
    addPanelRow(m_posePanel, poseLayout, tr("左肘"), m_poseLeftElbow);
    addPanelRow(m_posePanel, poseLayout, tr("右肩 前後"), m_poseRightShoulder);
    addPanelRow(m_posePanel, poseLayout, tr("右肩 開き"), m_poseRightShoulderRoll);
    addPanelRow(m_posePanel, poseLayout, tr("右肘"), m_poseRightElbow);
    addPanelRow(m_posePanel, poseLayout, tr("左脚 前後"), m_poseLeftHip);
    addPanelRow(m_posePanel, poseLayout, tr("左脚 開き"), m_poseLeftHipRoll);
    addPanelRow(m_posePanel, poseLayout, tr("左膝"), m_poseLeftKnee);
    addPanelRow(m_posePanel, poseLayout, tr("右脚 前後"), m_poseRightHip);
    addPanelRow(m_posePanel, poseLayout, tr("右脚 開き"), m_poseRightHipRoll);
    addPanelRow(m_posePanel, poseLayout, tr("右膝"), m_poseRightKnee);
    for (QDoubleSpinBox* spin : {m_poseTorsoPitch, m_poseTorsoRoll, m_poseHeadPitch, m_poseHeadYaw,
                                 m_poseLeftShoulder, m_poseLeftShoulderRoll, m_poseLeftElbow,
                                 m_poseRightShoulder, m_poseRightShoulderRoll, m_poseRightElbow,
                                 m_poseLeftHip, m_poseLeftHipRoll, m_poseLeftKnee, m_poseRightHip,
                                 m_poseRightHipRoll, m_poseRightKnee}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { applyPoseFromUi(); });
    }
    auto* poseKeyButton = new QPushButton(tr("現在コマにポーズキー"), m_posePanel);
    m_poseKeyButton = poseKeyButton;
    poseLayout->addWidget(poseKeyButton);
    connect(poseKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model || !isHumanoidKind(model->filePath)) return;
        model->poseKeys[m_viewport->frame()] = model->poseAt(m_viewport->frame());
        rebuildSheet();
        m_viewport->update();
        emit sceneEdited();
    });
    auto* poseKeyClearButton = new QPushButton(tr("ポーズキー削除"), m_posePanel);
    m_poseKeyClearButton = poseKeyClearButton;
    poseLayout->addWidget(poseKeyClearButton);
    connect(poseKeyClearButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model || !isHumanoidKind(model->filePath)) return;
        model->poseKeys.erase(m_viewport->frame());
        refreshPoseUi();
        rebuildSheet();
        m_viewport->update();
        emit sceneEdited();
    });
    auto* walkCycleButton = new QPushButton(tr("歩きループキー作成"), m_posePanel);
    m_walkCycleButton = walkCycleButton;
    poseLayout->addWidget(walkCycleButton);
    connect(walkCycleButton, &QPushButton::clicked, this, &PrevizWindow::addHumanoidWalkCycleKeys);
    poseLayout->addStretch(1);
    m_posePanel->hide();

    m_bodyPanel = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(m_bodyPanel);
    bodyLayout->setContentsMargins(8, 8, 8, 8);
    bodyLayout->setSpacing(6);
    auto* bodyLabel = new QLabel(tr("人型体型"), m_bodyPanel);
    m_bodyLabelWidget = bodyLabel;
    bodyLayout->addWidget(bodyLabel);
    auto* bodyPresetRow = new QWidget(m_bodyPanel);
    m_bodyPresetRow = bodyPresetRow;
    auto* bodyPresetLayout = new QHBoxLayout(bodyPresetRow);
    bodyPresetLayout->setContentsMargins(4, 0, 4, 0);
    m_bodyPresetCombo = new QComboBox(bodyPresetRow);
    m_bodyPresetCombo->addItems({tr("標準"), tr("大人"), tr("子供"), tr("がっしり"), tr("人外")});
    auto* applyBodyPresetButton = new QPushButton(tr("適用"), bodyPresetRow);
    bodyPresetLayout->addWidget(m_bodyPresetCombo, 1);
    bodyPresetLayout->addWidget(applyBodyPresetButton);
    bodyLayout->addWidget(bodyPresetRow);
    connect(applyBodyPresetButton, &QPushButton::clicked, this, [this] {
        applyHumanoidBodyPreset(m_bodyPresetCombo ? m_bodyPresetCombo->currentIndex() : 0);
    });

    const auto makeBodySpin = [this]() {
        auto* spin = new QDoubleSpinBox(m_bodyPanel);
        spin->setRange(0.20, 3.00);
        spin->setSingleStep(0.05);
        spin->setDecimals(2);
        spin->setSuffix(QObject::tr(" x"));
        spin->setFocusPolicy(Qt::ClickFocus);
        return spin;
    };
    bodyLayout->addWidget(new QLabel(tr("全体"), m_bodyPanel));
    m_bodyHeadScale = makeBodySpin();
    m_bodyHeadWidth = makeBodySpin();
    m_bodyHeadHeight = makeBodySpin();
    m_bodyHeadDepth = makeBodySpin();
    m_bodyFaceWidth = makeBodySpin();
    m_bodyFaceHeight = makeBodySpin();
    m_bodyFaceDepth = makeBodySpin();
    m_bodyTorsoLength = makeBodySpin();
    m_bodyChestHeight = makeBodySpin();
    m_bodyBellyHeight = makeBodySpin();
    m_bodyWaistHeight = makeBodySpin();
    m_bodyChestWidth = makeBodySpin();
    m_bodyBellyWidth = makeBodySpin();
    m_bodyWaistWidth = makeBodySpin();
    m_bodyChestDepth = makeBodySpin();
    m_bodyBellyDepth = makeBodySpin();
    m_bodyWaistDepth = makeBodySpin();
    m_bodyShoulderWidth = makeBodySpin();
    m_bodyHipWidth = makeBodySpin();
    m_bodyArmLength = makeBodySpin();
    m_bodyArmThickness = makeBodySpin();
    m_bodyArmDepth = makeBodySpin();
    m_bodyLegLength = makeBodySpin();
    m_bodyLegThickness = makeBodySpin();
    m_bodyLegDepth = makeBodySpin();
    m_bodyHandScale = makeBodySpin();
    m_bodyHandDepth = makeBodySpin();
    m_bodyFootScale = makeBodySpin();
    m_bodyFootDepth = makeBodySpin();
    addPanelRow(m_bodyPanel, bodyLayout, tr("胴体 全体高さ"), m_bodyTorsoLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腕 全体長さ"), m_bodyArmLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腕 全体太さ"), m_bodyArmThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腕 全体奥行き"), m_bodyArmDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("脚 全体長さ"), m_bodyLegLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("脚 全体太さ"), m_bodyLegThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("脚 全体奥行き"), m_bodyLegDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("手 全体サイズ"), m_bodyHandScale);
    addPanelRow(m_bodyPanel, bodyLayout, tr("手 奥行き"), m_bodyHandDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("足 全体サイズ"), m_bodyFootScale);
    addPanelRow(m_bodyPanel, bodyLayout, tr("足 奥行き"), m_bodyFootDepth);

    bodyLayout->addWidget(new QLabel(tr("頭・顔"), m_bodyPanel));
    addPanelRow(m_bodyPanel, bodyLayout, tr("頭 全体サイズ"), m_bodyHeadScale);
    addPanelRow(m_bodyPanel, bodyLayout, tr("頭 幅"), m_bodyHeadWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("頭 高さ"), m_bodyHeadHeight);
    addPanelRow(m_bodyPanel, bodyLayout, tr("頭 奥行き"), m_bodyHeadDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("顔 幅"), m_bodyFaceWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("顔 高さ"), m_bodyFaceHeight);
    addPanelRow(m_bodyPanel, bodyLayout, tr("顔 出っ張り"), m_bodyFaceDepth);

    bodyLayout->addWidget(new QLabel(tr("胴体"), m_bodyPanel));
    addPanelRow(m_bodyPanel, bodyLayout, tr("胸 高さ"), m_bodyChestHeight);
    addPanelRow(m_bodyPanel, bodyLayout, tr("胸 幅"), m_bodyChestWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("胸 奥行き"), m_bodyChestDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腹 高さ"), m_bodyBellyHeight);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腹 幅"), m_bodyBellyWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腹 奥行き"), m_bodyBellyDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腰 高さ"), m_bodyWaistHeight);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腰 幅"), m_bodyWaistWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("腰 奥行き"), m_bodyWaistDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("肩幅"), m_bodyShoulderWidth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("股幅"), m_bodyHipWidth);

    bodyLayout->addWidget(new QLabel(tr("腕 部位別"), m_bodyPanel));
    m_bodyLeftArmLength = makeBodySpin();
    m_bodyRightArmLength = makeBodySpin();
    m_bodyLeftArmThickness = makeBodySpin();
    m_bodyRightArmThickness = makeBodySpin();
    m_bodyLeftLegLength = makeBodySpin();
    m_bodyRightLegLength = makeBodySpin();
    m_bodyLeftLegThickness = makeBodySpin();
    m_bodyRightLegThickness = makeBodySpin();
    m_bodyLeftHandScale = makeBodySpin();
    m_bodyRightHandScale = makeBodySpin();
    m_bodyLeftFootScale = makeBodySpin();
    m_bodyRightFootScale = makeBodySpin();
    m_bodyLeftUpperArmLength = makeBodySpin();
    m_bodyRightUpperArmLength = makeBodySpin();
    m_bodyLeftForearmLength = makeBodySpin();
    m_bodyRightForearmLength = makeBodySpin();
    m_bodyLeftUpperArmThickness = makeBodySpin();
    m_bodyRightUpperArmThickness = makeBodySpin();
    m_bodyLeftForearmThickness = makeBodySpin();
    m_bodyRightForearmThickness = makeBodySpin();
    m_bodyLeftUpperArmDepth = makeBodySpin();
    m_bodyRightUpperArmDepth = makeBodySpin();
    m_bodyLeftForearmDepth = makeBodySpin();
    m_bodyRightForearmDepth = makeBodySpin();
    addPanelRow(m_bodyPanel, bodyLayout, tr("左二の腕 長さ"), m_bodyLeftUpperArmLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左二の腕 太さ"), m_bodyLeftUpperArmThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左二の腕 奥行き"), m_bodyLeftUpperArmDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひじ下 長さ"), m_bodyLeftForearmLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひじ下 太さ"), m_bodyLeftForearmThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひじ下 奥行き"), m_bodyLeftForearmDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右二の腕 長さ"), m_bodyRightUpperArmLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右二の腕 太さ"), m_bodyRightUpperArmThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右二の腕 奥行き"), m_bodyRightUpperArmDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひじ下 長さ"), m_bodyRightForearmLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひじ下 太さ"), m_bodyRightForearmThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひじ下 奥行き"), m_bodyRightForearmDepth);

    bodyLayout->addWidget(new QLabel(tr("脚 部位別"), m_bodyPanel));
    m_bodyLeftThighLength = makeBodySpin();
    m_bodyRightThighLength = makeBodySpin();
    m_bodyLeftShinLength = makeBodySpin();
    m_bodyRightShinLength = makeBodySpin();
    m_bodyLeftThighThickness = makeBodySpin();
    m_bodyRightThighThickness = makeBodySpin();
    m_bodyLeftShinThickness = makeBodySpin();
    m_bodyRightShinThickness = makeBodySpin();
    m_bodyLeftThighDepth = makeBodySpin();
    m_bodyRightThighDepth = makeBodySpin();
    m_bodyLeftShinDepth = makeBodySpin();
    m_bodyRightShinDepth = makeBodySpin();
    addPanelRow(m_bodyPanel, bodyLayout, tr("左太もも 長さ"), m_bodyLeftThighLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左太もも 太さ"), m_bodyLeftThighThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左太もも 奥行き"), m_bodyLeftThighDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひざ下 長さ"), m_bodyLeftShinLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひざ下 太さ"), m_bodyLeftShinThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("左ひざ下 奥行き"), m_bodyLeftShinDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右太もも 長さ"), m_bodyRightThighLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右太もも 太さ"), m_bodyRightThighThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右太もも 奥行き"), m_bodyRightThighDepth);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひざ下 長さ"), m_bodyRightShinLength);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひざ下 太さ"), m_bodyRightShinThickness);
    addPanelRow(m_bodyPanel, bodyLayout, tr("右ひざ下 奥行き"), m_bodyRightShinDepth);

    for (QDoubleSpinBox* spin : {m_bodyHeadScale, m_bodyHeadWidth, m_bodyHeadHeight, m_bodyHeadDepth,
                                 m_bodyFaceWidth, m_bodyFaceHeight, m_bodyFaceDepth, m_bodyTorsoLength,
                                 m_bodyChestHeight, m_bodyBellyHeight, m_bodyWaistHeight, m_bodyChestWidth,
                                 m_bodyBellyWidth, m_bodyWaistWidth, m_bodyChestDepth, m_bodyBellyDepth,
                                 m_bodyWaistDepth, m_bodyShoulderWidth, m_bodyHipWidth, m_bodyArmLength,
                                 m_bodyArmThickness, m_bodyArmDepth, m_bodyLegLength, m_bodyLegThickness,
                                 m_bodyLegDepth, m_bodyHandScale, m_bodyHandDepth, m_bodyFootScale,
                                 m_bodyFootDepth, m_bodyLeftArmLength, m_bodyRightArmLength,
                                 m_bodyLeftArmThickness, m_bodyRightArmThickness, m_bodyLeftLegLength,
                                 m_bodyRightLegLength, m_bodyLeftLegThickness, m_bodyRightLegThickness,
                                 m_bodyLeftHandScale, m_bodyRightHandScale, m_bodyLeftFootScale,
                                 m_bodyRightFootScale, m_bodyLeftUpperArmLength, m_bodyRightUpperArmLength,
                                 m_bodyLeftForearmLength, m_bodyRightForearmLength,
                                 m_bodyLeftUpperArmThickness, m_bodyRightUpperArmThickness,
                                 m_bodyLeftForearmThickness, m_bodyRightForearmThickness,
                                 m_bodyLeftUpperArmDepth, m_bodyRightUpperArmDepth, m_bodyLeftForearmDepth,
                                 m_bodyRightForearmDepth, m_bodyLeftThighLength, m_bodyRightThighLength,
                                 m_bodyLeftShinLength, m_bodyRightShinLength, m_bodyLeftThighThickness,
                                 m_bodyRightThighThickness, m_bodyLeftShinThickness, m_bodyRightShinThickness,
                                 m_bodyLeftThighDepth, m_bodyRightThighDepth, m_bodyLeftShinDepth,
                                 m_bodyRightShinDepth}) {
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) { applyBodyFromUi(); });
    }
    bodyLayout->addStretch(1);
    m_bodyPanel->hide();

    // モーションキー(カメラ/選択モデル): 現在コマにキーを打つ・消す
    setPoseControlsEnabled(false);
    setBodyControlsEnabled(false);

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
    auto* modelKeyClearButton = new QPushButton(tr("モデルキー削除"), container);
    layout->addWidget(modelKeyClearButton);
    connect(modelKeyClearButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model) return;
        model->transformKeys.erase(m_viewport->frame());
        if (isHumanoidKind(model->filePath)) model->poseKeys.erase(m_viewport->frame());
        rebuildSheet();
        m_viewport->update();
        refreshTransformUi();
        refreshPoseUi();
        emit sceneEdited();
    });
    connect(modelKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model) return;
        model->transformKeys[m_viewport->frame()] = model->transformAt(m_viewport->frame());
        if (isHumanoidKind(model->filePath)) {
            model->poseKeys[m_viewport->frame()] = model->poseAt(m_viewport->frame());
        }
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
        tr("作業視点: 右ドラッグ=軌道 / WASD+Q/E=移動 / 左ドラッグ=選択モデル移動(Shift=上下)"));
}

void PrevizWindow::togglePlayback() {
    m_playing = !m_playing;
    if (m_playing) {
        m_playAction->setText(tr("停止"));
        m_playTimer->start(1000 / std::max(1, m_playFpsSpin->value()));
    } else {
        m_playTimer->stop();
        m_playAction->setText(tr("再生"));
        rebuildSheet();
        emit frameChangeRequested(static_cast<int>(m_viewport->frame()));  // 停止位置を本体へ同期
    }
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
    m_rotX->setValue(tf.rotationDeg.x);
    m_rotY->setValue(tf.rotationDeg.y);
    m_rotZ->setValue(tf.rotationDeg.z);
    m_scaleX->setValue(tf.scale.x);
    m_scaleY->setValue(tf.scale.y);
    m_scaleZ->setValue(tf.scale.z);
    m_updating = false;
}

void PrevizWindow::applyTransformFromUi() {
    if (m_updating) return;
    core::PrevizModel* model = selectedModel();
    if (!model) return;

    core::PrevizTransform tf = model->transformAt(m_viewport->frame());
    tf.position = {static_cast<float>(m_posX->value()), static_cast<float>(m_posY->value()),
                   static_cast<float>(m_posZ->value())};
    tf.rotationDeg = {static_cast<float>(m_rotX->value()), static_cast<float>(m_rotY->value()),
                      static_cast<float>(m_rotZ->value())};
    // 非一様スケール(X/Y/Z個別)。これにより箱/円柱/球を任意の形へ変形できる
    tf.scale = {static_cast<float>(m_scaleX->value()), static_cast<float>(m_scaleY->value()),
               static_cast<float>(m_scaleZ->value())};

    // キーが無ければ基本配置、キーがあれば現在コマのキーを編集(カメラと同じ規則)
    if (model->transformKeys.empty()) {
        model->transform = tf;
    } else {
        model->transformKeys[m_viewport->frame()] = tf;
    }
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::openPoseWindow() {
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    if (!m_poseDialog) {
        m_poseDialog = new QDialog(this);
        m_poseDialog->setWindowTitle(tr("人型ポーズ"));
        m_poseDialog->resize(340, 620);
        auto* dialogLayout = new QVBoxLayout(m_poseDialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto* area = new QScrollArea(m_poseDialog);
        area->setWidgetResizable(true);
        area->setWidget(m_posePanel);
        dialogLayout->addWidget(area);
    }

    refreshPoseUi();
    if (m_posePanel) m_posePanel->show();
    m_poseDialog->show();
    m_poseDialog->raise();
    m_poseDialog->activateWindow();
}

void PrevizWindow::openBodyWindow() {
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    if (!m_bodyDialog) {
        m_bodyDialog = new QDialog(this);
        m_bodyDialog->setWindowTitle(tr("人型体型"));
        m_bodyDialog->resize(340, 680);
        auto* dialogLayout = new QVBoxLayout(m_bodyDialog);
        dialogLayout->setContentsMargins(0, 0, 0, 0);
        auto* area = new QScrollArea(m_bodyDialog);
        area->setWidgetResizable(true);
        area->setWidget(m_bodyPanel);
        dialogLayout->addWidget(area);
    }

    refreshBodyUi();
    if (m_bodyPanel) m_bodyPanel->show();
    m_bodyDialog->show();
    m_bodyDialog->raise();
    m_bodyDialog->activateWindow();
}

void PrevizWindow::setPoseControlsEnabled(bool enabled) {
    if (m_openPoseWindowButton) {
        m_openPoseWindowButton->setVisible(enabled);
        m_openPoseWindowButton->setEnabled(enabled);
    }
    if (!enabled && m_poseDialog) m_poseDialog->hide();
    if (m_posePanel) m_posePanel->setEnabled(enabled);
    for (QWidget* widget : {m_poseLabelWidget, m_posePresetRow, m_poseKeyButton, m_poseKeyClearButton,
                            m_walkCycleButton}) {
        if (widget) widget->setEnabled(enabled);
    }
    if (m_posePresetCombo) m_posePresetCombo->setEnabled(enabled);
    for (QDoubleSpinBox* spin : {m_poseTorsoPitch, m_poseTorsoRoll, m_poseHeadPitch, m_poseHeadYaw,
                                 m_poseLeftShoulder, m_poseLeftShoulderRoll, m_poseLeftElbow,
                                 m_poseRightShoulder, m_poseRightShoulderRoll, m_poseRightElbow,
                                 m_poseLeftHip, m_poseLeftHipRoll, m_poseLeftKnee, m_poseRightHip,
                                 m_poseRightHipRoll, m_poseRightKnee}) {
        if (spin) spin->setEnabled(enabled);
    }
}

void PrevizWindow::refreshPoseUi() {
    core::PrevizModel* model = selectedModel();
    const bool isHumanoid = model && isHumanoidKind(model->filePath);
    setPoseControlsEnabled(isHumanoid);
    if (!isHumanoid) return;

    m_updating = true;
    const core::PrevizHumanoidPose pose = model->poseAt(m_viewport->frame());
    m_poseTorsoPitch->setValue(pose.torsoPitchDeg);
    m_poseTorsoRoll->setValue(pose.torsoRollDeg);
    m_poseHeadPitch->setValue(pose.headPitchDeg);
    m_poseHeadYaw->setValue(pose.headYawDeg);
    m_poseLeftShoulder->setValue(pose.leftShoulderPitchDeg);
    m_poseLeftShoulderRoll->setValue(pose.leftShoulderRollDeg);
    m_poseLeftElbow->setValue(pose.leftElbowDeg);
    m_poseRightShoulder->setValue(pose.rightShoulderPitchDeg);
    m_poseRightShoulderRoll->setValue(pose.rightShoulderRollDeg);
    m_poseRightElbow->setValue(pose.rightElbowDeg);
    m_poseLeftHip->setValue(pose.leftHipPitchDeg);
    m_poseLeftHipRoll->setValue(pose.leftHipRollDeg);
    m_poseLeftKnee->setValue(pose.leftKneeDeg);
    m_poseRightHip->setValue(pose.rightHipPitchDeg);
    m_poseRightHipRoll->setValue(pose.rightHipRollDeg);
    m_poseRightKnee->setValue(pose.rightKneeDeg);
    m_updating = false;
}

core::PrevizHumanoidPose& PrevizWindow::editableHumanoidPose(core::PrevizModel& model) {
    if (model.poseKeys.empty()) return model.humanoidPose;
    model.poseKeys[m_viewport->frame()] = model.poseAt(m_viewport->frame());
    return model.poseKeys[m_viewport->frame()];
}

void PrevizWindow::applyPoseFromUi() {
    if (m_updating) return;
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    core::PrevizHumanoidPose& pose = editableHumanoidPose(*model);
    pose.torsoPitchDeg = static_cast<float>(m_poseTorsoPitch->value());
    pose.torsoRollDeg = static_cast<float>(m_poseTorsoRoll->value());
    pose.headPitchDeg = static_cast<float>(m_poseHeadPitch->value());
    pose.headYawDeg = static_cast<float>(m_poseHeadYaw->value());
    pose.leftShoulderPitchDeg = static_cast<float>(m_poseLeftShoulder->value());
    pose.leftShoulderRollDeg = static_cast<float>(m_poseLeftShoulderRoll->value());
    pose.leftElbowDeg = static_cast<float>(m_poseLeftElbow->value());
    pose.rightShoulderPitchDeg = static_cast<float>(m_poseRightShoulder->value());
    pose.rightShoulderRollDeg = static_cast<float>(m_poseRightShoulderRoll->value());
    pose.rightElbowDeg = static_cast<float>(m_poseRightElbow->value());
    pose.leftHipPitchDeg = static_cast<float>(m_poseLeftHip->value());
    pose.leftHipRollDeg = static_cast<float>(m_poseLeftHipRoll->value());
    pose.leftKneeDeg = static_cast<float>(m_poseLeftKnee->value());
    pose.rightHipPitchDeg = static_cast<float>(m_poseRightHip->value());
    pose.rightHipRollDeg = static_cast<float>(m_poseRightHipRoll->value());
    pose.rightKneeDeg = static_cast<float>(m_poseRightKnee->value());

    m_viewport->update();
    rebuildSheet();
    emit sceneEdited();
}

void PrevizWindow::setBodyControlsEnabled(bool enabled) {
    if (m_openBodyWindowButton) {
        m_openBodyWindowButton->setVisible(enabled);
        m_openBodyWindowButton->setEnabled(enabled);
    }
    if (!enabled && m_bodyDialog) m_bodyDialog->hide();
    if (m_bodyPanel) m_bodyPanel->setEnabled(enabled);
    for (QWidget* widget : {m_bodyLabelWidget, m_bodyPresetRow}) {
        if (widget) widget->setEnabled(enabled);
    }
    if (m_bodyPresetCombo) m_bodyPresetCombo->setEnabled(enabled);
    for (QDoubleSpinBox* spin : {m_bodyHeadScale, m_bodyHeadWidth, m_bodyHeadHeight, m_bodyHeadDepth,
                                 m_bodyFaceWidth, m_bodyFaceHeight, m_bodyFaceDepth, m_bodyTorsoLength,
                                 m_bodyChestHeight, m_bodyBellyHeight, m_bodyWaistHeight, m_bodyChestWidth,
                                 m_bodyBellyWidth, m_bodyWaistWidth, m_bodyChestDepth, m_bodyBellyDepth,
                                 m_bodyWaistDepth, m_bodyShoulderWidth, m_bodyHipWidth, m_bodyArmLength,
                                 m_bodyArmThickness, m_bodyArmDepth, m_bodyLegLength, m_bodyLegThickness,
                                 m_bodyLegDepth, m_bodyHandScale, m_bodyHandDepth, m_bodyFootScale,
                                 m_bodyFootDepth, m_bodyLeftArmLength, m_bodyRightArmLength,
                                 m_bodyLeftArmThickness, m_bodyRightArmThickness, m_bodyLeftLegLength,
                                 m_bodyRightLegLength, m_bodyLeftLegThickness, m_bodyRightLegThickness,
                                 m_bodyLeftHandScale, m_bodyRightHandScale, m_bodyLeftFootScale,
                                 m_bodyRightFootScale, m_bodyLeftUpperArmLength, m_bodyRightUpperArmLength,
                                 m_bodyLeftForearmLength, m_bodyRightForearmLength,
                                 m_bodyLeftUpperArmThickness, m_bodyRightUpperArmThickness,
                                 m_bodyLeftForearmThickness, m_bodyRightForearmThickness,
                                 m_bodyLeftUpperArmDepth, m_bodyRightUpperArmDepth, m_bodyLeftForearmDepth,
                                 m_bodyRightForearmDepth, m_bodyLeftThighLength, m_bodyRightThighLength,
                                 m_bodyLeftShinLength, m_bodyRightShinLength, m_bodyLeftThighThickness,
                                 m_bodyRightThighThickness, m_bodyLeftShinThickness, m_bodyRightShinThickness,
                                 m_bodyLeftThighDepth, m_bodyRightThighDepth, m_bodyLeftShinDepth,
                                 m_bodyRightShinDepth}) {
        if (spin) spin->setEnabled(enabled);
    }
}

void PrevizWindow::refreshBodyUi() {
    core::PrevizModel* model = selectedModel();
    const bool isHumanoid = model && isHumanoidKind(model->filePath);
    setBodyControlsEnabled(isHumanoid);
    if (!isHumanoid) return;

    m_updating = true;
    const core::PrevizHumanoidBody& body = model->humanoidBody;
    m_bodyHeadScale->setValue(body.headScale);
    m_bodyHeadWidth->setValue(body.headWidth);
    m_bodyHeadHeight->setValue(body.headHeight);
    m_bodyHeadDepth->setValue(body.headDepth);
    m_bodyFaceWidth->setValue(body.faceWidth);
    m_bodyFaceHeight->setValue(body.faceHeight);
    m_bodyFaceDepth->setValue(body.faceDepth);
    m_bodyTorsoLength->setValue(body.torsoLength);
    m_bodyChestHeight->setValue(body.chestHeight);
    m_bodyBellyHeight->setValue(body.bellyHeight);
    m_bodyWaistHeight->setValue(body.waistHeight);
    m_bodyChestWidth->setValue(body.chestWidth);
    m_bodyBellyWidth->setValue(body.bellyWidth);
    m_bodyWaistWidth->setValue(body.waistWidth);
    m_bodyChestDepth->setValue(body.chestDepth);
    m_bodyBellyDepth->setValue(body.bellyDepth);
    m_bodyWaistDepth->setValue(body.waistDepth);
    m_bodyShoulderWidth->setValue(body.shoulderWidth);
    m_bodyHipWidth->setValue(body.hipWidth);
    m_bodyArmLength->setValue(body.armLength);
    m_bodyArmThickness->setValue(body.armThickness);
    m_bodyArmDepth->setValue(body.armDepth);
    m_bodyLegLength->setValue(body.legLength);
    m_bodyLegThickness->setValue(body.legThickness);
    m_bodyLegDepth->setValue(body.legDepth);
    m_bodyHandScale->setValue(body.handScale);
    m_bodyHandDepth->setValue(body.handDepth);
    m_bodyFootScale->setValue(body.footScale);
    m_bodyFootDepth->setValue(body.footDepth);
    m_bodyLeftArmLength->setValue(body.leftArmLength);
    m_bodyRightArmLength->setValue(body.rightArmLength);
    m_bodyLeftArmThickness->setValue(body.leftArmThickness);
    m_bodyRightArmThickness->setValue(body.rightArmThickness);
    m_bodyLeftLegLength->setValue(body.leftLegLength);
    m_bodyRightLegLength->setValue(body.rightLegLength);
    m_bodyLeftLegThickness->setValue(body.leftLegThickness);
    m_bodyRightLegThickness->setValue(body.rightLegThickness);
    m_bodyLeftHandScale->setValue(body.leftHandScale);
    m_bodyRightHandScale->setValue(body.rightHandScale);
    m_bodyLeftFootScale->setValue(body.leftFootScale);
    m_bodyRightFootScale->setValue(body.rightFootScale);
    m_bodyLeftUpperArmLength->setValue(body.leftUpperArmLength);
    m_bodyRightUpperArmLength->setValue(body.rightUpperArmLength);
    m_bodyLeftForearmLength->setValue(body.leftForearmLength);
    m_bodyRightForearmLength->setValue(body.rightForearmLength);
    m_bodyLeftUpperArmThickness->setValue(body.leftUpperArmThickness);
    m_bodyRightUpperArmThickness->setValue(body.rightUpperArmThickness);
    m_bodyLeftForearmThickness->setValue(body.leftForearmThickness);
    m_bodyRightForearmThickness->setValue(body.rightForearmThickness);
    m_bodyLeftUpperArmDepth->setValue(body.leftUpperArmDepth);
    m_bodyRightUpperArmDepth->setValue(body.rightUpperArmDepth);
    m_bodyLeftForearmDepth->setValue(body.leftForearmDepth);
    m_bodyRightForearmDepth->setValue(body.rightForearmDepth);
    m_bodyLeftThighLength->setValue(body.leftThighLength);
    m_bodyRightThighLength->setValue(body.rightThighLength);
    m_bodyLeftShinLength->setValue(body.leftShinLength);
    m_bodyRightShinLength->setValue(body.rightShinLength);
    m_bodyLeftThighThickness->setValue(body.leftThighThickness);
    m_bodyRightThighThickness->setValue(body.rightThighThickness);
    m_bodyLeftShinThickness->setValue(body.leftShinThickness);
    m_bodyRightShinThickness->setValue(body.rightShinThickness);
    m_bodyLeftThighDepth->setValue(body.leftThighDepth);
    m_bodyRightThighDepth->setValue(body.rightThighDepth);
    m_bodyLeftShinDepth->setValue(body.leftShinDepth);
    m_bodyRightShinDepth->setValue(body.rightShinDepth);
    m_updating = false;
}

void PrevizWindow::applyBodyFromUi() {
    if (m_updating) return;
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    core::PrevizHumanoidBody& body = model->humanoidBody;
    body.headScale = static_cast<float>(m_bodyHeadScale->value());
    body.headWidth = static_cast<float>(m_bodyHeadWidth->value());
    body.headHeight = static_cast<float>(m_bodyHeadHeight->value());
    body.headDepth = static_cast<float>(m_bodyHeadDepth->value());
    body.faceWidth = static_cast<float>(m_bodyFaceWidth->value());
    body.faceHeight = static_cast<float>(m_bodyFaceHeight->value());
    body.faceDepth = static_cast<float>(m_bodyFaceDepth->value());
    body.torsoLength = static_cast<float>(m_bodyTorsoLength->value());
    body.chestHeight = static_cast<float>(m_bodyChestHeight->value());
    body.bellyHeight = static_cast<float>(m_bodyBellyHeight->value());
    body.waistHeight = static_cast<float>(m_bodyWaistHeight->value());
    body.chestWidth = static_cast<float>(m_bodyChestWidth->value());
    body.bellyWidth = static_cast<float>(m_bodyBellyWidth->value());
    body.waistWidth = static_cast<float>(m_bodyWaistWidth->value());
    body.chestDepth = static_cast<float>(m_bodyChestDepth->value());
    body.bellyDepth = static_cast<float>(m_bodyBellyDepth->value());
    body.waistDepth = static_cast<float>(m_bodyWaistDepth->value());
    body.shoulderWidth = static_cast<float>(m_bodyShoulderWidth->value());
    body.hipWidth = static_cast<float>(m_bodyHipWidth->value());
    body.armLength = static_cast<float>(m_bodyArmLength->value());
    body.armThickness = static_cast<float>(m_bodyArmThickness->value());
    body.armDepth = static_cast<float>(m_bodyArmDepth->value());
    body.legLength = static_cast<float>(m_bodyLegLength->value());
    body.legThickness = static_cast<float>(m_bodyLegThickness->value());
    body.legDepth = static_cast<float>(m_bodyLegDepth->value());
    body.handScale = static_cast<float>(m_bodyHandScale->value());
    body.handDepth = static_cast<float>(m_bodyHandDepth->value());
    body.footScale = static_cast<float>(m_bodyFootScale->value());
    body.footDepth = static_cast<float>(m_bodyFootDepth->value());
    body.leftArmLength = static_cast<float>(m_bodyLeftArmLength->value());
    body.rightArmLength = static_cast<float>(m_bodyRightArmLength->value());
    body.leftArmThickness = static_cast<float>(m_bodyLeftArmThickness->value());
    body.rightArmThickness = static_cast<float>(m_bodyRightArmThickness->value());
    body.leftLegLength = static_cast<float>(m_bodyLeftLegLength->value());
    body.rightLegLength = static_cast<float>(m_bodyRightLegLength->value());
    body.leftLegThickness = static_cast<float>(m_bodyLeftLegThickness->value());
    body.rightLegThickness = static_cast<float>(m_bodyRightLegThickness->value());
    body.leftHandScale = static_cast<float>(m_bodyLeftHandScale->value());
    body.rightHandScale = static_cast<float>(m_bodyRightHandScale->value());
    body.leftFootScale = static_cast<float>(m_bodyLeftFootScale->value());
    body.rightFootScale = static_cast<float>(m_bodyRightFootScale->value());
    body.leftUpperArmLength = static_cast<float>(m_bodyLeftUpperArmLength->value());
    body.rightUpperArmLength = static_cast<float>(m_bodyRightUpperArmLength->value());
    body.leftForearmLength = static_cast<float>(m_bodyLeftForearmLength->value());
    body.rightForearmLength = static_cast<float>(m_bodyRightForearmLength->value());
    body.leftUpperArmThickness = static_cast<float>(m_bodyLeftUpperArmThickness->value());
    body.rightUpperArmThickness = static_cast<float>(m_bodyRightUpperArmThickness->value());
    body.leftForearmThickness = static_cast<float>(m_bodyLeftForearmThickness->value());
    body.rightForearmThickness = static_cast<float>(m_bodyRightForearmThickness->value());
    body.leftUpperArmDepth = static_cast<float>(m_bodyLeftUpperArmDepth->value());
    body.rightUpperArmDepth = static_cast<float>(m_bodyRightUpperArmDepth->value());
    body.leftForearmDepth = static_cast<float>(m_bodyLeftForearmDepth->value());
    body.rightForearmDepth = static_cast<float>(m_bodyRightForearmDepth->value());
    body.leftThighLength = static_cast<float>(m_bodyLeftThighLength->value());
    body.rightThighLength = static_cast<float>(m_bodyRightThighLength->value());
    body.leftShinLength = static_cast<float>(m_bodyLeftShinLength->value());
    body.rightShinLength = static_cast<float>(m_bodyRightShinLength->value());
    body.leftThighThickness = static_cast<float>(m_bodyLeftThighThickness->value());
    body.rightThighThickness = static_cast<float>(m_bodyRightThighThickness->value());
    body.leftShinThickness = static_cast<float>(m_bodyLeftShinThickness->value());
    body.rightShinThickness = static_cast<float>(m_bodyRightShinThickness->value());
    body.leftThighDepth = static_cast<float>(m_bodyLeftThighDepth->value());
    body.rightThighDepth = static_cast<float>(m_bodyRightThighDepth->value());
    body.leftShinDepth = static_cast<float>(m_bodyLeftShinDepth->value());
    body.rightShinDepth = static_cast<float>(m_bodyRightShinDepth->value());

    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::applyHumanoidPosePreset(int presetIndex) {
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    editableHumanoidPose(*model) = humanoidPosePreset(presetIndex);
    refreshPoseUi();
    rebuildSheet();
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::applyHumanoidBodyPreset(int presetIndex) {
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    model->humanoidBody = humanoidBodyPreset(presetIndex);
    refreshBodyUi();
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::addHumanoidWalkCycleKeys() {
    core::PrevizModel* model = selectedModel();
    if (!model || !isHumanoidKind(model->filePath)) return;

    const size_t start = m_viewport->frame();
    const size_t lastFrame = m_frameCount > 0 ? m_frameCount - 1 : start;
    const size_t mid = std::min(lastFrame, start + 12);
    const size_t end = std::min(lastFrame, start + 24);
    model->poseKeys[start] = humanoidPosePreset(1);
    model->poseKeys[mid] = humanoidPosePreset(2);
    model->poseKeys[end] = humanoidPosePreset(1);
    refreshPoseUi();
    rebuildSheet();
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::debugSetSelectedScale(double sx, double sy, double sz) {
    if (!selectedModel()) return;
    // UIのスピンボックス経由で設定する(valueChangedシグナルでapplyTransformFromUiが走り、
    // 実際のユーザー操作と同じ経路で非一様スケールが反映される)
    m_scaleX->setValue(sx);
    m_scaleY->setValue(sy);
    m_scaleZ->setValue(sz);
}

void PrevizWindow::debugSetSelectedPosition(double x, double y, double z) {
    if (!selectedModel()) return;
    m_posX->setValue(x);
    m_posY->setValue(y);
    m_posZ->setValue(z);
}

void PrevizWindow::debugSetLensDistortion(double d) {
    if (m_distortSpin) m_distortSpin->setValue(d);
}

void PrevizWindow::debugSetHumanoidPosePreset(int presetIndex) {
    applyHumanoidPosePreset(presetIndex);
}

void PrevizWindow::debugSetHumanoidBodyPreset(int presetIndex) {
    applyHumanoidBodyPreset(presetIndex);
}

void PrevizWindow::debugAddHumanoidWalkCycleKeys() {
    addHumanoidWalkCycleKeys();
}

void PrevizWindow::addPrimitive(const QString& kind, bool select) {
    if (!m_scene) return;
    // kind(":box"/":cylinder"/":sphere")に応じた表示名を決める
    QString label = tr("箱");
    if (kind == QStringLiteral(":cylinder")) label = tr("円柱");
    else if (kind == QStringLiteral(":sphere")) label = tr("球");
    else if (kind == QStringLiteral(":humanoid")) label = tr("人型");
    else if (kind == QStringLiteral(":humanoid_box")) label = tr("人型ロボット");

    core::PrevizModel primitive;
    int number = 1;
    for (const auto& model : m_scene->models) {
        if (model.filePath == kind.toStdString()) ++number;
    }
    primitive.name = tr("%1 %2").arg(label).arg(number).toStdString();
    primitive.filePath = kind.toStdString();  // 組み込みプリミティブ
    m_scene->models.push_back(std::move(primitive));
    refreshModelList();
    if (select) m_modelList->setCurrentRow(static_cast<int>(m_scene->models.size()) - 1);
    rebuildSheet();
    m_viewport->update();
    emit sceneEdited();
}

void PrevizWindow::setScene(core::PrevizScene* scene) {
    m_scene = scene;
    m_viewport->setScene(scene);
    // 空のシーンには最初から操作できる箱を1つ置く(目安キューブの実体化)
    if (m_scene && m_scene->models.empty()) {
        addPrimitive(QStringLiteral(":box"), true);
    }
    refreshModelList();
    refreshCameraUi();
    refreshPoseUi();
    refreshBodyUi();
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
    refreshPoseUi();
    rebuildSheet();
}

void PrevizWindow::addModel() {
    if (!m_scene) return;
    const QString path =
        QFileDialog::getOpenFileName(this, tr("3Dモデルを開く"), QString(), tr("3Dモデル (*.glb *.gltf *.stl)"));
    if (path.isEmpty()) return;

    core::PrevizModel model;
    model.name = QFileInfo(path).completeBaseName().toStdString();
    model.filePath = path.toStdString();
    m_scene->models.push_back(std::move(model));

    refreshModelList();
    m_modelList->setCurrentRow(static_cast<int>(m_scene->models.size()) - 1);  // 追加直後は選択状態にする
    m_viewport->update();
    rebuildSheet();
    emit sceneEdited();
}

void PrevizWindow::debugAddModelFile(const QString& path) {
    // 動作確認用: QFileDialogを使わずaddModel()と同じ手順(モデル追加→選択→リフレッシュ)を踏む
    if (!m_scene || path.isEmpty()) return;

    core::PrevizModel model;
    model.name = QFileInfo(path).completeBaseName().toStdString();
    model.filePath = path.toStdString();
    m_scene->models.push_back(std::move(model));

    refreshModelList();
    m_modelList->setCurrentRow(static_cast<int>(m_scene->models.size()) - 1);
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
    if (m_distortSpin) m_distortSpin->setValue(m_scene->camera.lensDistortion);
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
                const size_t frame = static_cast<size_t>(f);
                flags << (model.transformKeys.count(frame) > 0 || model.poseKeys.count(frame) > 0);
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
        const bool hasKey = model.transformKeys.count(f) > 0 || model.poseKeys.count(f) > 0;
        if (hasKey) {
            model.transformKeys.erase(f);
            model.poseKeys.erase(f);
        } else {
            model.transformKeys[f] = model.transformAt(f);
            if (isHumanoidKind(model.filePath)) model.poseKeys[f] = model.poseAt(f);
        }
    }
    rebuildSheet();
    m_viewport->update();
    refreshCameraUi();
    refreshTransformUi();
    refreshPoseUi();
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
