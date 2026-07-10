#pragma once

#include <QMainWindow>

#include "core/Previz.h"

class PrevizViewport;
class QDoubleSpinBox;
class QLabel;
class QListWidget;

// プリビズウィンドウ(別ウィンドウ)。3Dモデルの配置とカメラ(焦点距離/画角)の設定を行い、
// 作画ウィンドウとはコマ番号で連動する。
class PrevizWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PrevizWindow(QWidget* parent = nullptr);

    // シーンの差し替え(新規/読込後)。所有権は持たない
    void setScene(core::PrevizScene* scene);
    void setFrame(size_t frame);
    PrevizViewport* viewport() const { return m_viewport; }

signals:
    void sceneEdited();  // シーンが変更された(MainWindowが未保存フラグを立てる)

private:
    void addModel();
    void removeSelectedModel();
    void refreshModelList();
    void refreshCameraUi();
    void refreshTransformUi();
    core::PrevizModel* selectedModel();
    void applyTransformFromUi();

    core::PrevizScene* m_scene = nullptr;
    PrevizViewport* m_viewport = nullptr;
    QListWidget* m_modelList = nullptr;
    QDoubleSpinBox* m_focalSpin = nullptr;
    QLabel* m_fovLabel = nullptr;
    // 選択モデルのトランスフォーム編集(位置XYZ/ヨー回転/等倍スケール)
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_rotY = nullptr;
    QDoubleSpinBox* m_scale = nullptr;
    bool m_updating = false;
};
