#pragma once

#include <QMainWindow>
#include <QStringList>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace core {
class Cut;
class Project;
}

// 撮影ウィンドウ(別ウィンドウ)。カット単位のエフェクトスタックを「撮影シート」
// (行=エフェクト、列=コマ)で管理する。エフェクトのパラメータはコマ単位のキーで
// 時間変化させられる(キー間は線形補間、キーが無ければ基本値)。
// 右側に選択コマのエフェクト適用済みプレビューを常時表示する
class ShootingWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ShootingWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // カット一覧・エフェクト一覧・シート・プレビューを作り直す
    void refresh();
    // プレビュー合成に使うキャンバスサイズ(MainWindowの作画キャンバスと同じ寸法を渡す)
    void setCanvasSize(int width, int height);

    // 表示対象カットを指定する(メインのアクティブカットに合わせる用)
    void setCutIndex(int index);

    // 動作確認用: シートの選択コマを直接指定する
    void debugSelectKoma(int koma);

signals:
    void edited();  // エフェクトの構成/有効/対象/パラメータ/キーのいずれかが変更された

private:
    core::Cut* currentCut() const;  // 選択中カット(無ければnullptr)

    void rebuildEffectList();  // エフェクト一覧を作り直す(選択は維持を試みる)
    void rebuildSheet();       // 撮影シート(行=エフェクト、列=コマ)を作り直す
    void syncSelectionUI();    // 選択中エフェクト×選択中コマに合わせてパラメータ編集・キー表示を更新
    void rebuildParamForm();   // 選択中エフェクトのparamsAt(選択コマ)でスピン群を作り直す
    void updateKeyStateLabel();
    void updatePreview();      // 選択コマをrenderCutFrameしてプレビューへ表示する

    void addEffectOfType(int typeInt);
    void removeSelected();
    void moveSelected(int delta);
    void onListCheckChanged(QListWidgetItem* item);
    void onTargetIndexChanged(int index);
    void onParamValueChanged(const std::string& key, double value);
    void addKeyAtCurrentKoma();
    void removeKeyAtCurrentKoma();
    void onSheetCellChanged(int row, int column);
    void onSheetCellDoubleClicked(int row, int column);
    void markEdited();  // m_dirty相当の通知+シート/プレビューの追従

    // --- クラシック撮影(マルチプレーン)パネル ---
    void rebuildMultiplanePanel();    // カメラ値+段テーブルを選択中カットの内容で作り直す
    void onMultiplaneToggled(bool checked);
    void onMultiplaneCameraChanged();  // カメラ/サンプル数スピンのいずれかが変わった
    void addMultiplanePlaneRow();
    void removeMultiplanePlaneRow();

    core::Project* m_project = nullptr;
    int m_cutIndex = 0;   // 表示対象カット
    int m_effectRow = -1;  // 選択中エフェクト(-1=なし)
    int m_koma = 0;        // 選択中コマ(0始まり)

    QComboBox* m_cutCombo = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QWidget* m_paramContainer = nullptr;
    QFormLayout* m_paramForm = nullptr;
    QPushButton* m_addKeyButton = nullptr;
    QPushButton* m_removeKeyButton = nullptr;
    QLabel* m_keyStateLabel = nullptr;
    QTableWidget* m_sheet = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_komaLabel = nullptr;

    // クラシック撮影(マルチプレーン撮影台)パネル
    QGroupBox* m_multiplaneGroup = nullptr;
    QDoubleSpinBox* m_mpFocalSpin = nullptr;
    QDoubleSpinBox* m_mpSensorSpin = nullptr;
    QDoubleSpinBox* m_mpFStopSpin = nullptr;
    QDoubleSpinBox* m_mpFocusSpin = nullptr;
    QSpinBox* m_mpSamplesSpin = nullptr;
    QTableWidget* m_mpTable = nullptr;
    QPushButton* m_mpAddButton = nullptr;
    QPushButton* m_mpRemoveButton = nullptr;

    // キー持ちエフェクトのスピン編集は即データへ書かず、「キー追加」で確定する(プリビズのキー規則)。
    // その保留値。エフェクト/コマ選択が変わるたびに破棄される
    std::map<std::string, double> m_pendingParams;

    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;
    bool m_updating = false;  // 表示反映中はシグナル・編集処理を抑止する
};
