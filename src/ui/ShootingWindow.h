#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QStringList>
#include <map>
#include <string>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;
class GLCanvas;
class FilmCurveWidget;

namespace core {
class Cut;
class Effect;
class Project;
struct MultiplaneBacklight;
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
    // 動作確認用: 再生の開始/停止をトグルする(実際のQTimer駆動のtogglePlayback()をそのまま呼ぶ)
    void debugTogglePlayback();
    // 動作確認用: 指定エフェクトのマスク編集ダイアログを開く(既存が開いていれば閉じてから開き直す)
    void debugOpenMaskEditDialog(int effectIndex);
    // 動作確認用: 現在開いているマスク編集ダイアログ(未オープンならnullptr)
    QWidget* maskEditDialogWidget() const;

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

    // フィルムエフェクトの層別応答カーブウィジェット参照(コマ移動時の軽量更新用)。
    // buildEffectGroupBox内でeffect.type==Filmの時だけ1個作られる
    struct FilmCurveRowWidgets {
        int effectIndex = -1;
        FilmCurveWidget* widget = nullptr;
    };

    // タイムライン1行分の意味づけ。エフェクトごとに「見出し行」1つ+その直下に
    // キー持ちパラメータの「プロパティ行」が並ぶ(After Effectsのレイヤータイムライン風)。
    // multiplane有効時はエフェクト行群の後に「撮影台」セクション(Stand*)が常時表示で続く
    struct TimelineRow {
        enum class Kind { Header, Param, StandHeader, StandFocal, StandFocus, StandFStop, StandBacklight };
        Kind kind = Kind::Param;
        int effectIndex = -1;
        std::string key;      // Param行のみ使用
        int backlightIndex = -1;  // StandBacklight行のみ使用(灯のindex)
    };

    core::Cut* currentCut() const;

    // エフェクト1個分のGroupBoxを作る(対象コンボ・上下/削除ボタン・パラメータ行群)
    QGroupBox* buildEffectGroupBox(int effectIndex, const QStringList& celNames);

    // 指定エフェクトのマスク(適用範囲)をペンで塗るモードレスダイアログを開く。
    // 既に開いていれば閉じてから開き直す
    void openMaskEditDialog(int effectIndex);
    // マスク編集ダイアログが開いていれば閉じる(エフェクト構成が変わるrebuildEffectControls
    // の直前に呼び、GLCanvasが束縛しているeffect.maskへの生ポインタが無効化するのを防ぐ)
    void closeMaskEditDialogIfOpen();
    // 対象エフェクトのmaskが空(未設定)なら、キャンバスサイズの透明ビットマップを確保する
    void ensureMaskAllocated(core::Effect& effect) const;

    void rebuildEffectControls();  // 左「エフェクトコントロール」パネルをカットの内容で作り直す(構造が変わる時)
    // パネル+タイムラインの作り直しを次のイベントループへ遅延させる(連続要求は1回にまとめる)。
    // ウィジェットのシグナル処理中にrebuildEffectControls()を直接呼ぶと発信元ウィジェットを
    // delete してしまいクラッシュするため、シグナルハンドラからは必ずこちらを使うこと
    void scheduleRebuild();
    // タイムラインだけを次のイベントループへ遅延・合流して作り直す(エフェクトコントロールは
    // 作り直さない=スピン編集中のフォーカスを守る)。スピンのvalueChangedのような高頻度発火元
    // から直接rebuildTimeline()を呼ぶのは避け、こちらを使うこと
    void scheduleTimelineRebuild();
    void refreshParamRowValues();  // 構造は変えずスピン値/◆表示だけを現在コマに合わせて更新する(軽量、再生中用)
    void rebuildTimeline();        // 下段タイムライン(キー持ちプロパティのみ行を作る)を作り直す
    void refreshTimelineHighlight();  // 行の作り直し無しでCTI列のハイライトだけ更新する
    // プレビュー更新を要求する(デバウンス)。連続する変更は最後の1回だけ実描画されるため、
    // スピン連打やスクラブでも重い合成が積み上がらない
    void requestPreview();
    void renderPreviewNow();       // 現在コマをrenderCutFrameしてプレビューへ即時表示する(実処理)
    // m_previewQuality(プレビュー長辺の絶対px上限)とキャンバスサイズから実際にcore::RenderOptions::
    // proxyScaleへ渡す値を計算する。比率ではなく絶対px上限にすることで、キャンバスが4K等でも
    // プレビューの実解像度(=重さ)がほぼ一定になる(1.0を超えない=拡大はしない)
    double computeProxyScale() const;
    // 現在コマの見た目を決める全状態を文字列化する(コマ指紋)。同じ指紋なら同じ絵になるため、
    // renderPreviewNow()はこれをキーにm_frameCacheを引き、ヒットすれば合成を丸ごと省略する
    QString frameFingerprint(const core::Cut& cut, int koma) const;
    void clearFrameCache();  // m_frameCacheを全クリアする(構造変更・画質変更・カット切替時)
    void updateTransportLabel();  // 「コマ n / N (t s) 描画Xms/キャッシュ」ラベルを更新する(m_lastRenderNoteを付記)

    // 現在コマ(CTI)を変更する。範囲外はクランプ。タイムライン/プレビュー同期。
    // lightweight=trueの場合(再生中)はrefreshParamRowValues()を省略する(左パネルのスピン値
    // 同期はフォーカスの無い再生中は不要な負荷なので、停止時に一度フル同期すれば十分)
    void setKoma(int koma, bool lightweight = false);

    void addEffectOfType(int typeInt);
    void removeEffect(int effectIndex);
    void moveEffect(int effectIndex, int delta);
    void onEffectEnabledChanged(int effectIndex, bool enabled);
    void onEffectTargetChanged(int effectIndex, int comboIndex);
    // 開始/終了コマ(in/out点)スピンの変更。表示は1始まりなので内部の0始まりへ変換する。
    // 再構築は不要(タイムラインの色帯だけ更新すれば見た目に反映される)
    void onEffectStartFrameChanged(int effectIndex, int displayValue);
    // 終了コマ: 0(specialValueText「末尾」)は内部-1(カット末尾まで)
    void onEffectEndFrameChanged(int effectIndex, int displayValue);
    // ストップウォッチのON/OFF。ONで現在コマに現在値のキーを1個打つ、OFFで全キーを消す
    void onStopwatchToggled(int effectIndex, const std::string& key, bool checked);
    // スピンの値変更。hasCurveなら現在コマへ自動でキーを打つ、そうでなければ基本値を直接更新
    void onParamSpinChanged(int effectIndex, const std::string& key, double value);
    // 効果コントロール上の◆ボタン: 現在コマのキーをトグルする(タイムラインのダブルクリックと同じ)
    void onKeyDiamondClicked(int effectIndex, const std::string& key);

    // --- フィルムエフェクトの層別応答カーブ(FilmCurveWidget) ---
    // ドラッグ中/確定のたびに発火。onParamSpinChangedと同じ規則(hasCurveなら現在コマへキー、
    // 無ければparams直接)でrespR{idx}/respG{idx}/respB{idx}を更新する
    void onFilmCurveChanged(int effectIndex, int layer, int pointIndex, double value);
    // リセットボタン: 該当層(layer=-1なら全層)のresp5点を恒等へ戻す
    void onFilmCurveResetRequested(int effectIndex, int layer);
    // 「プリセット」コンボ: フィルム銘柄の特徴(resp15+基本値)をまとめて適用する
    void onFilmPresetSelected(int effectIndex, int presetIndex);
    // 開いているFilmCurveWidgetがあれば現在コマのresp値へ更新する(refreshParamRowValues相当の軽量経路)
    void refreshFilmCurveWidgets();

    void onTimelineCellClicked(int row, int column);
    void onTimelineCellDoubleClicked(int row, int column);
    void onTimelineHeaderClicked(int column);

    void togglePlayback();
    void onPlaybackTick();

    // --- クラシック撮影(マルチプレーン)パネル ---
    void rebuildMultiplanePanel();    // カメラ値+段テーブル+フレーミング+灯一覧を選択中カットの内容で作り直す
    void onMultiplaneToggled(bool checked);
    void onMultiplaneCameraChanged();  // カメラ/サンプル数スピンのいずれかが変わった
    void addMultiplanePlaneRow();
    void removeMultiplanePlaneRow();

    // --- フレーミング固定(「写る幅で固定」) ---
    void onFramingLockToggled(bool checked);  // ONでセンサー幅スピンを無効化(値は写る幅/基準距離から導出)
    void onFramingChanged();                  // 写る幅/基準距離スピンのいずれかが変わった

    // --- 透過光(T光)パネル(クラシック撮影グループ内、複数灯) ---
    // 選択中の灯(m_backlightIndex)を返す(カット無し/範囲外ならnullptr)
    core::MultiplaneBacklight* selectedBacklight() const;
    // 灯一覧(QListWidget)をカットの内容で作り直す(構造変更、灯追加/削除/名前変更ボタンから直接呼ぶ)。
    // 選択インデックスをクランプして維持し、末尾でrebuildBacklightParamsPanel()も呼ぶ
    void rebuildBacklightList();
    // 選択中の灯のパラメータ群(強度/色/透過率/にじみ/マスク)をスピン等へ反映する(構造は変えない軽量更新)
    void rebuildBacklightParamsPanel();
    void onBacklightSelectionChanged(int row);  // 灯リストの選択行が変わった(データ変更ではないのでmarkEditedしない)
    void onBacklightItemChanged(QListWidgetItem* item);  // 灯リストのチェック(enabled)がトグルされた
    void onBacklightAddClicked();
    void onBacklightRemoveClicked();
    void onBacklightRenameClicked();
    void onBacklightChanged();  // 選択中の灯の強度/色/塗料透過率/にじみのいずれかが変わった
    // 光源色スウォッチ(見本)ボタンがクリックされた: QColorDialogで選び、スピン(0〜1)へ反映する
    void onBacklightColorSwatchClicked();
    // 光源色スピン(R/G/B)いずれかの表示直後にスウォッチの背景色を合わせる(値の反映はonBacklightChanged側)
    void updateBacklightColorSwatch();

    // 光源マスク(ペンで塗った範囲/セル・レイヤー指定、選択中の灯にバインド)
    void ensureBacklightMaskAllocated(core::MultiplaneBacklight& backlight) const;
    void openBacklightMaskEditDialog();       // マスクをペンで塗るモードレスダイアログを開く
    void closeBacklightMaskDialogIfOpen();    // カット切替・灯の構成変更等でBitmap*束縛が無効化する前に閉じる
    void onBacklightMaskCelChanged(int comboIndex);    // 「マスクセル」コンボの変更
    void onBacklightMaskLayerChanged(int comboIndex);  // 「マスクレイヤー」コンボの変更
    void refreshBacklightMaskLayerCombo();  // マスクセルの選択に応じて「マスクレイヤー」コンボの項目を作り直す

    // --- クラシック撮影のコマキー(点滅=透過光強度、滑らかな変化=焦点距離/フォーカス距離/絞り) ---
    // 現在コマ(m_koma)へのキー追加/削除。追加時はスピンの現在表示値をそのまま登録する
    void onIntensityKeyAddClicked();
    void onIntensityKeyRemoveClicked();
    void onFocalKeyAddClicked();
    void onFocalKeyRemoveClicked();
    void onFocusKeyAddClicked();
    void onFocusKeyRemoveClicked();
    void onFStopKeyAddClicked();
    void onFStopKeyRemoveClicked();
    // コマ移動時、キー持ちの透過光強度(選択中の灯)/焦点距離/フォーカス距離/絞りスピンを
    // 現在コマの補間値へ同期し、キー追加/削除ボタンの有効状態を更新する(構造は変えない軽量更新、
    // rebuildMultiplanePanel/rebuildBacklightParamsPanel内でも使う)
    void refreshMultiplaneKeyedFields();

    void markEdited();  // 現在コマのプレビュー更新+シグナル送出(データ変更の共通後処理)

    core::Project* m_project = nullptr;
    int m_cutIndex = 0;   // 表示対象カット
    int m_koma = 0;        // 現在コマ(CTI、0始まり)

    QComboBox* m_cutCombo = nullptr;
    QComboBox* m_previewQualityCombo = nullptr;  // プレビュー画質(フル/1/2/1/4)

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
    QSpinBox* m_mpExportSamplesSpin = nullptr;
    QTableWidget* m_mpTable = nullptr;
    QPushButton* m_mpAddButton = nullptr;
    QPushButton* m_mpRemoveButton = nullptr;
    // 焦点距離/フォーカス距離/絞り(F値)のコマキー追加・削除(滑らかなカメラ変化用)
    QToolButton* m_mpFocalKeyAddButton = nullptr;
    QToolButton* m_mpFocalKeyRemoveButton = nullptr;
    QToolButton* m_mpFocusKeyAddButton = nullptr;
    QToolButton* m_mpFocusKeyRemoveButton = nullptr;
    QToolButton* m_mpFStopKeyAddButton = nullptr;
    QToolButton* m_mpFStopKeyRemoveButton = nullptr;

    // フレーミング固定(「写る幅で固定」): 焦点距離を変えても基準距離での構図が変わらないようにする
    QCheckBox* m_mpFramingLockCheck = nullptr;
    QDoubleSpinBox* m_mpFramingWidthSpin = nullptr;
    QDoubleSpinBox* m_mpFramingRefDistanceSpin = nullptr;

    // 透過光(T光)パネル(クラシック撮影グループ内、複数灯)
    QGroupBox* m_backlightGroup = nullptr;
    // 灯一覧(チェック=enabled+灯名)+追加/削除/名前変更ボタン
    QListWidget* m_blList = nullptr;
    QPushButton* m_blAddButton = nullptr;
    QPushButton* m_blRemoveButton = nullptr;
    QPushButton* m_blRenameButton = nullptr;
    int m_backlightIndex = -1;  // 選択中の灯(cut->multiplane().backlightsのindex、-1=未選択)

    // 選択中の灯のパラメータ群
    QDoubleSpinBox* m_blIntensitySpin = nullptr;
    // 強度のコマキー追加・削除(蛍光灯/液晶の点滅用、選択中の灯へ)
    QToolButton* m_blIntensityKeyAddButton = nullptr;
    QToolButton* m_blIntensityKeyRemoveButton = nullptr;
    QDoubleSpinBox* m_blColorRSpin = nullptr;
    QDoubleSpinBox* m_blColorGSpin = nullptr;
    QDoubleSpinBox* m_blColorBSpin = nullptr;
    QPushButton* m_blColorSwatch = nullptr;  // 光源色の見本(クリックでQColorDialog)
    QDoubleSpinBox* m_blTransmittanceSpin = nullptr;
    QDoubleSpinBox* m_blBloomRadiusSpin = nullptr;
    QDoubleSpinBox* m_blBloomStrengthSpin = nullptr;

    // 光源マスク(ペンで塗った範囲/セル・レイヤー指定、選択中の灯へ)
    QToolButton* m_blMaskEditButton = nullptr;
    QPushButton* m_blMaskClearButton = nullptr;
    QComboBox* m_blMaskCelCombo = nullptr;
    QComboBox* m_blMaskLayerCombo = nullptr;
    // T光マスク編集ダイアログ(開いていなければnullptr)。QDialogはWA_DeleteOnCloseで自動破棄される
    QDialog* m_backlightMaskDialog = nullptr;

    // 右: プレビュー+トランスポート
    QLabel* m_previewLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QLabel* m_komaLabel = nullptr;
    QTimer* m_playTimer = nullptr;
    QTimer* m_previewTimer = nullptr;  // プレビュー更新のデバウンス用(singleShot)
    bool m_playing = false;            // 再生中はプレビューをデバウンスせず各コマ直接描く
    QElapsedTimer m_playElapsed;       // 再生開始からの経過時間(実時間維持のフレームスキップに使う)
    int m_playStartKoma = 0;           // 再生開始時のコマ(経過時間からの相対計算の基準)

    // 下段: タイムライン(After Effects風「エフェクトレイヤー」形式。行=エフェクト見出し+
    // キー持ちプロパティ、列=コマ)
    QTableWidget* m_timeline = nullptr;
    // タイムライン行の意味づけ一覧(m_timelineの行indexと対応)
    std::vector<TimelineRow> m_timelineRows;

    // マスク編集ダイアログ(開いていなければnullptr)。QDialogはWA_DeleteOnCloseで自動破棄される
    QDialog* m_maskEditDialog = nullptr;
    int m_maskEditEffectIndex = -1;  // ダイアログが対象としているエフェクトindex

    // エフェクトコントロールパネルの現在のパラメータ行(コマ移動時の軽量更新に使う)
    std::vector<ParamRowWidgets> m_paramRows;
    // 開いているフィルムエフェクトの応答カーブウィジェット(コマ移動時の軽量更新に使う)
    std::vector<FilmCurveRowWidgets> m_filmCurveRows;

    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;
    bool m_updating = false;  // 表示反映中はシグナル・編集処理を抑止する
    bool m_rebuildScheduled = false;  // scheduleRebuild()の多重予約防止
    bool m_timelineRebuildScheduled = false;  // scheduleTimelineRebuild()の多重予約防止

    // プレビュー画質(プレビュー長辺の絶対px上限。フル=900, 1/2=600, 1/4=360)。
    // computeProxyScale()がキャンバスサイズと合わせてcore::RenderOptions::proxyScaleへの
    // 実際の値を算出する
    double m_previewQuality = 600.0;
    // コマ指紋→描画済みプレビューのキャッシュ。同じ絵になるコマの再合成を省略する
    QHash<QString, QPixmap> m_frameCache;
    static constexpr int kFrameCacheLimit = 200;  // これを超えたら単純にclear()する

    QString m_lastRenderNote;  // 直近のrenderPreviewNow()の所要時間表示(「描画 12ms」/「キャッシュ」)
};
