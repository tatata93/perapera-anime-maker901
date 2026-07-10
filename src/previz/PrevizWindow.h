#pragma once

#include <QMainWindow>
#include <functional>

#include "core/Previz.h"

class PrevizViewport;
class PrevizSheetPanel;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QTimer;
class QToolButton;

// プリビズウィンドウ(別ウィンドウ)。3Dモデルの配置とカメラ(焦点距離/画角)の設定を行い、
// 作画ウィンドウとはコマ番号で連動する。
class PrevizWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PrevizWindow(QWidget* parent = nullptr);

    // シーンの差し替え(新規/読込後)。所有権は持たない
    void setScene(core::PrevizScene* scene);
    void setFrame(size_t frame);
    // 現在コマと尺(コマ数)を反映する。プリビズシートの行数もここで揃える
    void setTimeline(size_t currentFrame, size_t frameCount);
    PrevizViewport* viewport() const { return m_viewport; }

signals:
    void sceneEdited();          // シーンが変更された(MainWindowが未保存フラグを立てる)
    void frameChangeRequested(int frame);  // シートのセルクリックで本体のコマ移動を要求する

private:
    void addModel();
    void addBoxModel(bool select);  // 組み込みの箱(:box)を追加。目安キューブの実体化
    void removeSelectedModel();
    void refreshModelList();
    void refreshCameraUi();
    void refreshTransformUi();
    core::PrevizModel* selectedModel();
    void applyTransformFromUi();

    // プリビズシート(カメラ列+モデル列)の内容を組み立てて反映する
    void rebuildSheet();
    void onSheetCellClicked(int column, int frame);
    void onSheetKeyToggleRequested(int column, int frame);

    // 十字リモコン(ナッジ操作)のキー規則ヘルパー。キーが無ければ基本状態、
    // あれば現在コマのキーを編集する(PrevizViewportの操作と同じ規則)
    core::PrevizCameraState& editableCamera();
    core::PrevizTransform& editableModelTransform(core::PrevizModel& model);
    // 対象(カメラ/選択モデル)に応じてcameraFn/modelFnのどちらかを適用し、UI・シートを更新する
    void applyNudge(const std::function<void(core::PrevizCameraState&)>& cameraFn,
                    const std::function<void(core::PrevizTransform&)>& modelFn);

    core::PrevizScene* m_scene = nullptr;
    PrevizViewport* m_viewport = nullptr;
    PrevizSheetPanel* m_sheetPanel = nullptr;
    QListWidget* m_modelList = nullptr;
    QDoubleSpinBox* m_focalSpin = nullptr;
    QLabel* m_fovLabel = nullptr;
    // 選択モデルのトランスフォーム編集(位置XYZ/ヨー回転/等倍スケール)
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_rotY = nullptr;
    QDoubleSpinBox* m_scale = nullptr;
    // 十字リモコン(ナッジ操作)
    QComboBox* m_nudgeTargetCombo = nullptr;  // 0=カメラ、1=選択モデル
    QDoubleSpinBox* m_moveStepSpin = nullptr;
    QDoubleSpinBox* m_rotStepSpin = nullptr;
    QToolButton* m_pitchUpButton = nullptr;
    QToolButton* m_pitchDownButton = nullptr;
    size_t m_frameCount = 1;  // 尺(コマ数)。シートの行数に使う
    bool m_updating = false;

    // プリビズ内再生(カメラ/モデルのモーション確認)。本体とは独立に回し、停止時にコマを同期する
    void togglePlayback();
    QTimer* m_playTimer = nullptr;
    QAction* m_playAction = nullptr;
    QSpinBox* m_playFpsSpin = nullptr;
    bool m_playing = false;
};
