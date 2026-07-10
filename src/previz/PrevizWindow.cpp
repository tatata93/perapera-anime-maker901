#include "PrevizWindow.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

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
        emit sceneEdited();
    });
    auto* cameraKeyClearButton = new QPushButton(tr("カメラキー削除"), container);
    layout->addWidget(cameraKeyClearButton);
    connect(cameraKeyClearButton, &QPushButton::clicked, this, [this] {
        if (!m_scene) return;
        m_scene->camera.keys.erase(m_viewport->frame());
        m_viewport->update();
        emit sceneEdited();
    });
    auto* modelKeyButton = new QPushButton(tr("現在コマにモデルキー"), container);
    layout->addWidget(modelKeyButton);
    connect(modelKeyButton, &QPushButton::clicked, this, [this] {
        core::PrevizModel* model = selectedModel();
        if (!model) return;
        model->transformKeys[m_viewport->frame()] = model->transformAt(m_viewport->frame());
        emit sceneEdited();
    });

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
}

void PrevizWindow::setFrame(size_t frame) {
    m_viewport->setFrame(frame);
    refreshCameraUi();
    refreshTransformUi();  // モーションキーがあるとコマごとに配置が変わる
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
    emit sceneEdited();
}

void PrevizWindow::removeSelectedModel() {
    if (!m_scene) return;
    const int row = m_modelList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_scene->models.size())) return;
    m_scene->models.erase(m_scene->models.begin() + row);
    refreshModelList();
    m_viewport->update();
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
