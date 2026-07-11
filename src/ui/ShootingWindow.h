#pragma once

#include <QMainWindow>
#include <QStringList>
#include <map>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace core {
class Cut;
class Effect;
class Project;
}

// 撮影ウィンドウ(別ウィンドウ)。After Effects風の上下2段レイアウト:
// 上段=左「エフェクトコントロール」パネル+右「プレビュー」、下段=「タイムライン」パネル。
// パラメータはストップウォッチでキーフレーム化でき(AE同様、キー持ちパラメータはスピン編集の
// たびに現在コマへ自動でキーを打つ)、キー間は線形補間される(core::Effectのparamsに従う)。
class ShootingWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ShootingWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // カット一覧・エフェクトコントロール・タイムライン・プレビューを作り直す
    void refresh();
    // プレビュー合成に使うキャンバスサイズ(MainWindowの作画キャンバスと同じ寸法を渡す)
    void setCanvasSize(int width, int height);

    // 表示対象カットを指定する(メインのアクティブカットに合わせる用)
    void setCutIndex(int index);

    // 動作確認用: 現在コマ(CTI)を直接指定する
    void debugSelectKoma(int koma);

signals:
    void edited();  // エフェクトの構成/有効/対象/パラメータ/キーのいずれかが変更された

private:
    // エフェクトコントロールパネルの1パラメータ行のウィジェット参照(コマ移動時の軽量更新用)
    struct ParamRowWidgets {
        int effectIndex = -1;
        std::string key;
        QToolButton* stopwatch = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QToolButton* diamond = nullptr;
    };

    core::Cut* currentCut() const;

    // エフェクト1個分のGroupBoxを作る(対象コンボ・上下/削除ボタン・パラメータ行群)
    QGroupBox* buildEffectGroupBox(int effectIndex, const QStringList& celNames);

    void rebuildEffectControls();  // 左「エフェクトコントロール」パネルをカットの内容で作り直す(構造が変わる時)
    void refreshParamRowValues();  // 構造は変えずスピン値/◆表示だけを現在コマに合わせて更新する(軽量、再生中用)
    void rebuildTimeline();        // 下段タイムライン(キー持ちプロパティのみ行を作る)を作り直す
    void refreshTimelineHighlight();  // 行の作り直し無しでCTI列のハイライトだけ更新する
    void updatePreview();          // 現在コマをrenderCutFrameしてプレビューへ表示する
    void updateTransportLabel();   // 「コマ n / N (t s)」ラベルを更新する

    void setKoma(int koma);  // 現在コマ(CTI)を変更する。範囲外はクランプ。タイムライン/プレビュー同期

    void addEffectOfType(int typeInt);
    void removeEffect(int effectIndex);
    void moveEffect(int effectIndex, int delta);
    void onEffectEnabledChanged(int effectIndex, bool enabled);
    void onEffectTargetChanged(int effectIndex, int comboIndex);
    // ストップウォッチのON/OFF。ONで現在コマに現在値のキーを1個打つ、OFFで全キーを消す
    void onStopwatchToggled(int effectIndex, const std::string& key, bool checked);
    // スピンの値変更。hasCurveなら現在コマへ自動でキーを打つ、そうでなければ基本値を直接更新
    void onParamSpinChanged(int effectIndex, const std::string& key, double value);
    // 効果コントロール上の◆ボタン: 現在コマのキーをトグルする(タイムラインのダブルクリックと同じ)
    void onKeyDiamondClicked(int effectIndex, const std::string& key);

    void onTimelineCellClicked(int row, int column);
    void onTimelineCellDoubleClicked(int row, int column);
    void onTimelineHeaderClicked(int column);

    void togglePlayback();
    void onPlaybackTick();

    // --- クラシック撮影(マルチプレーン)パネル ---
    void rebuildMultiplanePanel();    // カメラ値+段テーブルを選択中カットの内容で作り直す
    void onMultiplaneToggled(bool checked);
    void onMultiplaneCameraChanged();  // カメラ/サンプル数スピンのいずれかが変わった
    void addMultiplanePlaneRow();
    void removeMultiplanePlaneRow();

    void markEdited();  // 現在コマのプレビュー更新+シグナル送出(データ変更の共通後処理)

    core::Project* m_project = nullptr;
    int m_cutIndex = 0;   // 表示対象カット
    int m_koma = 0;        // 現在コマ(CTI、0始まり)

    QComboBox* m_cutCombo = nullptr;

    // 左: エフェクトコントロールパネル
    QScrollArea* m_effectScroll = nullptr;
    QWidget* m_effectContainer = nullptr;
    QVBoxLayout* m_effectContainerLayout = nullptr;
    QPushButton* m_addEffectButton = nullptr;

    // クラシック撮影(マルチプレーン撮影台)パネル(左パネル最下部)
    QGroupBox* m_multiplaneGroup = nullptr;
    QDoubleSpinBox* m_mpFocalSpin = nullptr;
    QDoubleSpinBox* m_mpSensorSpin = nullptr;
    QDoubleSpinBox* m_mpFStopSpin = nullptr;
    QDoubleSpinBox* m_mpFocusSpin = nullptr;
    QSpinBox* m_mpSamplesSpin = nullptr;
    QTableWidget* m_mpTable = nullptr;
    QPushButton* m_mpAddButton = nullptr;
    QPushButton* m_mpRemoveButton = nullptr;

    // 右: プレビュー+トランスポート
    QLabel* m_previewLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QLabel* m_komaLabel = nullptr;
    QTimer* m_playTimer = nullptr;

    // 下段: タイムライン(行=キー持ちプロパティ、列=コマ)
    QTableWidget* m_timeline = nullptr;
    // タイムライン行 → (エフェクトindex, パラメータキー)。hasCurveな行のみ生成される
    std::vector<std::pair<int, std::string>> m_timelineRows;

    // エフェクトコントロールパネルの現在のパラメータ行(コマ移動時の軽量更新に使う)
    std::vector<ParamRowWidgets> m_paramRows;

    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;
    bool m_updating = false;  // 表示反映中はシグナル・編集処理を抑止する
};
