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

    // 動作確認用: プリミティブを追加する(kind=":box"/":cylinder"/":sphere")。追加後に選択状態にする
    void debugAddPrimitive(const QString& kind) { addPrimitive(kind, true); }
    // 動作確認用: ダイアログを出さずにファイルパスからモデルを追加する(addModel()と同じ経路)。
    // 追加後に選択状態にし、ビューポートを更新する
    void debugAddModelFile(const QString& path);
    // 動作確認用: 現在選択中モデルのスケールXYZをUI経由で設定する(非一様スケールの変形確認用)
    void debugSetSelectedScale(double sx, double sy, double sz);
    // 動作確認用: 現在選択中モデルの位置XYZをUI経由で設定する(デモで形状同士を離して見せる用)
    void debugSetSelectedPosition(double x, double y, double z);
    // 動作確認用: レンズ歪曲量を設定する(魚眼/樽/糸巻きの確認用)
    void debugSetLensDistortion(double d);
    void debugSetHumanoidPosePreset(int presetIndex);
    void debugSetHumanoidBodyPreset(int presetIndex);
    void debugAddHumanoidWalkCycleKeys();

signals:
    void sceneEdited();          // シーンが変更された(MainWindowが未保存フラグを立てる)
    void frameChangeRequested(int frame);  // シートのセルクリックで本体のコマ移動を要求する

private:
    void addModel();
    // 組み込みプリミティブ(kind=":box"/":cylinder"/":sphere")を追加する
    void addPrimitive(const QString& kind, bool select);
    void removeSelectedModel();
    void refreshModelList();
    void refreshCameraUi();
    void refreshTransformUi();
    void refreshPoseUi();
    void refreshBodyUi();
    core::PrevizModel* selectedModel();
    void applyTransformFromUi();
    void applyPoseFromUi();
    void applyBodyFromUi();

    // プリビズシート(カメラ列+モデル列)の内容を組み立てて反映する
    void rebuildSheet();
    void onSheetCellClicked(int column, int frame);
    void onSheetKeyToggleRequested(int column, int frame);

    // 十字リモコン(ナッジ操作)のキー規則ヘルパー。キーが無ければ基本状態、
    // あれば現在コマのキーを編集する(PrevizViewportの操作と同じ規則)
    core::PrevizCameraState& editableCamera();
    core::PrevizTransform& editableModelTransform(core::PrevizModel& model);
    core::PrevizHumanoidPose& editableHumanoidPose(core::PrevizModel& model);
    void applyHumanoidPosePreset(int presetIndex);
    void applyHumanoidBodyPreset(int presetIndex);
    void addHumanoidWalkCycleKeys();
    void setPoseControlsEnabled(bool enabled);
    void setBodyControlsEnabled(bool enabled);
    // 対象(カメラ/選択モデル)に応じてcameraFn/modelFnのどちらかを適用し、UI・シートを更新する
    void applyNudge(const std::function<void(core::PrevizCameraState&)>& cameraFn,
                    const std::function<void(core::PrevizTransform&)>& modelFn);

    core::PrevizScene* m_scene = nullptr;
    PrevizViewport* m_viewport = nullptr;
    PrevizSheetPanel* m_sheetPanel = nullptr;
    QListWidget* m_modelList = nullptr;
    QDoubleSpinBox* m_focalSpin = nullptr;
    QLabel* m_fovLabel = nullptr;
    QDoubleSpinBox* m_distortSpin = nullptr;  // レンズ歪曲(魚眼/樽/糸巻き)
    // 選択モデルのトランスフォーム編集(位置XYZ/回転XYZ/スケールXYZ=任意変形対応)
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_rotX = nullptr;
    QDoubleSpinBox* m_rotY = nullptr;
    QDoubleSpinBox* m_rotZ = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    QDoubleSpinBox* m_scaleZ = nullptr;
    QComboBox* m_posePresetCombo = nullptr;
    QDoubleSpinBox* m_poseTorsoPitch = nullptr;
    QDoubleSpinBox* m_poseHeadYaw = nullptr;
    QDoubleSpinBox* m_poseLeftShoulder = nullptr;
    QDoubleSpinBox* m_poseLeftElbow = nullptr;
    QDoubleSpinBox* m_poseRightShoulder = nullptr;
    QDoubleSpinBox* m_poseRightElbow = nullptr;
    QDoubleSpinBox* m_poseLeftHip = nullptr;
    QDoubleSpinBox* m_poseLeftKnee = nullptr;
    QDoubleSpinBox* m_poseRightHip = nullptr;
    QDoubleSpinBox* m_poseRightKnee = nullptr;
    QComboBox* m_bodyPresetCombo = nullptr;
    QDoubleSpinBox* m_bodyHeadScale = nullptr;
    QDoubleSpinBox* m_bodyTorsoLength = nullptr;
    QDoubleSpinBox* m_bodyChestWidth = nullptr;
    QDoubleSpinBox* m_bodyBellyWidth = nullptr;
    QDoubleSpinBox* m_bodyWaistWidth = nullptr;
    QDoubleSpinBox* m_bodyShoulderWidth = nullptr;
    QDoubleSpinBox* m_bodyHipWidth = nullptr;
    QDoubleSpinBox* m_bodyArmLength = nullptr;
    QDoubleSpinBox* m_bodyArmThickness = nullptr;
    QDoubleSpinBox* m_bodyLegLength = nullptr;
    QDoubleSpinBox* m_bodyLegThickness = nullptr;
    QDoubleSpinBox* m_bodyHandScale = nullptr;
    QDoubleSpinBox* m_bodyFootScale = nullptr;
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
