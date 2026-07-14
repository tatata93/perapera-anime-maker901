#pragma once

#include <QColor>
#include <QMainWindow>
#include <QPointF>
#include <QStringList>
#include <cstdint>
#include <memory>
#include <set>

#include "core/CommandStack.h"
#include "core/Project.h"

class CameraPanel;
class CelPanel;
class EditWindow;
class FramePanel;
class GLCanvas;
class LayerPanel;
class PalettePanel;
class PrevizWindow;
class ProjectManagerWindow;
class ReferencePanel;
class SettingBoardWindow;
class ShootingWindow;
class StoryboardWindow;
class TapPanel;
class XsheetPanel;
class QCheckBox;
class QCloseEvent;
class QImage;
class QComboBox;
class QDialog;
class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

namespace core {
struct RenderOptions;
struct SaveOptions;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    GLCanvas* canvas() const { return m_canvas; }

    // キャンバス(作画用紙)の解像度。プロジェクト設定に従う(既定1920x1080)。
    // m_projectがまだ生成されていない一瞬(コンストラクタ冒頭)のみフォールバックする
    int canvasWidth() const { return m_project ? m_project->canvasWidth() : 1920; }
    int canvasHeight() const { return m_project ? m_project->canvasHeight() : 1080; }

    // 解像度設定確認用: プロジェクトのキャンバスサイズを直接変更する(ダイアログを出さない)
    void debugSetCanvasSize(int width, int height);
    // 解像度設定確認用: 「プロジェクト設定...」ダイアログを非モーダルで開き、そのポインタを返す
    // (ダイアログが乗った状態のウィンドウ全体をスクリーンショットするテスト用)
    QDialog* debugOpenCanvasSizeDialog();

    // オニオンスキン確認用: 3フレームに縦線を描き、中央フレームを表示する
    void debugSetupOnionDemo();
    // 再生確認用
    void debugTogglePlayback() { togglePlayback(); }
    // ファイルI/O確認用(ダイアログを出さない)
    bool debugSaveTo(const QString& path) { return saveToFile(path); }
    bool debugLoadFrom(const QString& path) { return loadFromFile(path); }
    void debugNewDocument() { newDocument(); }
    // Undo/Redo確認用
    void debugUndo() { undo(); }
    void debugRedo() { redo(); }
    // 下敷き確認用: ファイルダイアログを出さずに指定パスから下敷き連番を設定する
    void debugSetUnderlayFile(const QString& path);
    // レイヤー確認用: レイヤー2枚(下=赤縦線/上=青横線)のデモを作る
    void debugSetupLayerDemo();
    void debugSetLayerVisible(int layerIndex, bool visible);
    // タイムシート確認用: オニオンデモの後、尺6コマ・2コマ打ち(動画1,1,2,2,3,3)を設定する
    void debugSetupXsheetDemo();
    // タイムシート確認用: setCurrentFrame()の公開ラッパー
    void debugSetCurrentFrame(size_t frame) { setCurrentFrame(frame); }
    // セル管理確認用: セルBを追加し、セルAに赤縦線・セルBに青横線を描く(共にコマ1に動画1を割付)
    void debugSetupCelDemo();
    void debugSetCelVisible(int celIndex, bool visible);
    // 動画削除確認用: deleteDrawing()の公開ラッパー
    void debugDeleteDrawing(int idx) { deleteDrawing(idx); }
    // 動画複製確認用: duplicateDrawing()の公開ラッパー
    void debugDuplicateDrawing(int idx) { duplicateDrawing(idx); }
    // ライトテーブル確認用: FramePanelのチェック操作を経由せず、指定した動画一覧を直接透かし表示に設定する
    void debugSetLightTable(const QList<int>& drawings);
    // オニオンスキン確認用: 有効/無効を直接切り替える
    void debugSetOnionEnabled(bool enabled);
    // 塗りつぶし確認用: 閉じた矩形枠を現在フレームに描く
    void debugSetupFillDemo();
    // 塗分け線確認用: 矩形枠+色トレス線レイヤー(赤縦線)を作り、彩色レイヤーをアクティブにする
    void debugSetupColorTraceDemo();
    void debugSetCleanView(bool enabled);
    // 自動保存確認用: performAutosave()を即実行し、保存先パスを返す(失敗時は空文字)
    QString debugTriggerAutosave();
    // カラーパレット確認用: パレットに3色追加→保存→新規→読込を行い、往復が正しければ0、不一致なら1を返す
    // (projectPathは.ppprojフォルダのパス)
    int debugPaletteRoundTrip(const QString& projectPath);
    // レイヤー種別確認用: レイヤーを2枚追加(計3枚)し種別を設定→保存→新規→読込を行い、
    // 往復が正しければ0、不一致なら1を返す(projectPathは.ppprojフォルダのパス)
    int debugRoleRoundTrip(const QString& projectPath);

    // 書き出し確認用: タイムシートデモ(尺6・2コマ打ち)を組んでから全コマを連番PNGで書き出す。
    // 成功時0、失敗時1を返す
    int debugExportSequence(const QString& dir);
    // 動作確認用: アクティブカットのコマ0を指定モード(0=作画/1=プリビズ/2=両方)で1枚書き出す
    bool debugExportFrame(const QString& pngPath, int mode, bool transparent);
    // 動作確認用: 全カット通しを連番PNGで書き出す。書き出したコマ数を返す(0=失敗)
    int debugExportAllCuts(const QString& dir);
    // タップ移動確認用: 1枚の動画を尺3で止めにし、位置キー(コマ1=原点、コマ3=右下)を打つ
    void debugSetupTapDemo();
    // カメラフレーム確認用: ストローク1本+コマ0(中心・100%)とコマ23(左上寄り・50%)に
    // カメラキーを打ち、コマ12へ移動する
    void debugSetupCameraDemo();
    // プリビズ確認用: プリビズウィンドウを開く
    void debugOpenPreviz() { openPrevizWindow(); }
    // プリビズ下敷き確認用
    void debugSetPrevizUnderlay(bool enabled) { setPrevizUnderlay(enabled); }
    PrevizWindow* previzWindow() const { return m_previzWindow; }

    // 絵コンテウィンドウ確認用
    void debugOpenStoryboard() { openStoryboardWindow(); }
    StoryboardWindow* storyboardWindow() const { return m_storyboardWindow; }
    // 絵コンテデモ確認用: パネル2枚(共にカット番号"1"、尺36/12)を追加し、
    // パネル1のdrawingに赤い斜め線を描く
    void debugSetupStoryboardDemo();

    // 設定ボードウィンドウ確認用
    void debugOpenSettingBoard() { openSettingBoardWindow(); }
    SettingBoardWindow* settingBoardWindow() const { return m_settingBoardWindow; }
    // 設定ボードデモ確認用: ボード2枚追加(「キャラ: 主人公」「美術: 教室」)し、
    // 1枚目に赤い線を描いて参照ドックで1枚目を選択する
    void debugSetupSettingBoardDemo();
    ReferencePanel* referencePanel() const { return m_referencePanel; }

    // 引きセル確認用: アクティブセルを横2倍(キャンバス幅の2倍)にリサイズし、
    // 左右半分に色違いのストロークを描いた上で、位置キー(コマ0=0、コマ2=-キャンバス幅)を打ち、
    // 尺3で止めてからコマ2へ移動する
    void debugSetupOversizeDemo();

    // 編集(カッティング)ウィンドウ確認用
    void debugOpenEditWindow() { openEditWindow(); }
    EditWindow* editWindow() const { return m_editWindow; }
    void debugOpenProjectManagerWindow() { openProjectManagerWindow(); }
    ProjectManagerWindow* projectManagerWindow() const { return m_projectManagerWindow; }
    // 編集デモ確認用: カット3つ(尺12/24/12、進捗: 原画/レイアウト/未着手)を組み、
    // カット1に赤ストローク・カット2に別ストロークを描いてから編集ウィンドウを開き、
    // グローバルコマ18(カット2内)へシークする
    void debugSetupEditDemo();

    // 撮影ウィンドウ確認用
    ShootingWindow* shootingWindow() const { return m_shootingWindow; }
    // 撮影デモ確認用: ストローク1本を描いて尺24の止めにし、全体ブラー(コマ0=半径0→
    // コマ23=半径10のキー、画面右半分だけに効くマスク付き)+全体パラ(キー無し)を組んで
    // 撮影ウィンドウを開き、コマ12を選択する
    void debugSetupShootingDemo();

    // クラシック撮影(マルチプレーン撮影台)デモ確認用: セルA=赤い矩形枠(距離500mm/幅400mm)、
    // セルB=左寄り緑丸(距離300mm/幅300mm)を作り、マルチプレーン撮影を有効化(f/2.0・
    // フォーカス500mm・samples=8)して撮影ウィンドウを開く
    void debugSetupClassicDemo();

    // 透過光(T光)デモ確認用: セルを黒で全面塗りして中央付近に丸い穴(消しゴム)をいくつか開け、
    // クラシック撮影(距離500mm/幅400mm、f/2.0・フォーカス250mm=穴はピンボケ=玉ボケになる)+
    // 透過光ON(強度4、にじみ半径24/強さ0.8)にして撮影ウィンドウを開く
    void debugSetupBacklightDemo();

    // フィルムエフェクト確認用: グレー地にカラーバー風の縦帯数本を描いた止めセル(尺4)を組み、
    // 全体にフィルムエフェクト(既定値、粒状を目視しやすくするためgrain=0.5)をかけて
    // 撮影ウィンドウを開く。コマ1・コマ2をdebugSelectKomaで切り替えて撮ると、
    // 粒状パターンがフレームごとに変わっている(=無相関な擬似乱数)ことを目視確認できる
    void debugSetupFilmDemo();

    // 統合デモ確認用: これまで実装した全機能を盛り込んだ3カット構成のプロジェクトを
    // プログラム的に構築する(絵コンテ2枚・設定ボード1枚のデータも含む)。
    // カット1: PAN(引きセル位置キー)+T.U.(カメラキー)+グロー、尺48
    // カット2: クラシック撮影(マルチプレーン、被写界深度)+黒パラ、尺36
    // カット3: シェイク(収束キー)+オレンジパラ、尺24
    void debugBuildFullDemo();
    // 統合デモ書き出し確認用: scene(0)の全カット・全コマをグローバル連番で最終画レンダリングし、
    // ffmpegでmp4Pathへ連結書き出しする(ヘッドレス実行のためダイアログは出さない)。
    // 各カットの中間コマの最終画をmp4と同じフォルダへfulldemo_cut1.png等として保存する
    // (mp4生成の成否によらず保存される)。ffmpegが見つからない/失敗した場合は
    // 連番PNG(固定フォルダ)を残したままfalseを返す
    bool exportAllCutsMovie(const QString& mp4Path, int fps);

    // クラッシュリカバリ: 自動保存ファイルが残っていれば復元するか確認する。
    // ヘッドレステスト実行時にダイアログを出さないようmain.cppから条件付きで呼ばれる
    void checkAutosaveRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createNewDocument();
    void setupToolBar();
    void setupMenus();
    void setupPanels();

    void newDocument();
    // 作成オプション(名前・解像度・FPS)をダイアログで決めて新規プロジェクトを作る
    void onNewProject();
    // 新規/読込でプロジェクトを差し替えた後の共通後処理(各ウィンドウ・パネルの追従)
    void finishProjectReplacement();
    // options=nullptr(既定)なら全ファイル書き出し(名前を付けて保存/新規保存先はこちら)
    bool saveToFile(const QString& path, const core::SaveOptions* options = nullptr);
    bool loadFromFile(const QString& path);
    void saveAs();
    void save();
    void open();
    void updateWindowTitle();
    void undo();
    void redo();

    // 保存スコープ(部分保存)の管理: markXxxDirty系ヘルパーで変更箇所を記録し、
    // 上書き保存(save())時にどのファイルだけ書き直せばよいか判断する。
    // 判断に迷う・複数箇所に波及しうる変更はmarkAllDirty()で安全側(全書き)に倒す
    void markProjectDirty();     // 構造(カット追加/削除/並べ替え/改名/シーン)+パレット
    void markStoryboardDirty();  // 絵コンテ
    void markBoardsDirty();      // 設定ボード
    void markCutDirty(core::Cut& cut);  // 個別カットの変更(idが未採番(0)ならallCuts扱いにする)
    void markAllDirty();         // 安全側: 全ファイルを書き直す

    core::Cut& activeCut();
    core::Cel& activeCel();
    core::Layer& activeLayer();
    void updateCanvasLayers();
    void setCurrentFrame(size_t index);
    void addFrameAfterCurrent();
    void deleteCurrentFrame();
    void togglePlayback();
    void onPlaybackTick();
    void updateOnionSkin();
    void updateFrameLabel();
    // コマ打ちパターン適用(1/2/3コマ打ち)。XsheetPanelのボタンと「操作」メニューのショートカット(1/2/3)で共有する
    void applyStepPattern(int step);

    // 動画(絵)管理操作
    void deleteDrawing(int idx);
    void duplicateDrawing(int idx);

    // ライトテーブル(任意動画の透かし表示)
    void updateLightTable();
    std::vector<const core::Bitmap*> collectLightTableBitmaps(const QList<int>& drawings);

    // レイヤーパネル操作
    void updateLayerPanel();
    void addLayerToActiveCel();
    void removeActiveLayer();
    void moveActiveLayer(int delta);

    // カラーパレットパネル操作
    void updatePalettePanel();
    void addCurrentColorToPalette();
    void removeSelectedPaletteColor();

    // タイムシート(Xsheet)パネル操作
    void updateXsheetPanel();

    // セルパネル操作(セルの可視/非表示をワンクリックで切り替える一覧)
    void updateCelPanel();

    // タップパネル操作(アクティブセルの位置キー一覧)
    void updateTapPanel();

    // カメラパネル操作(カットのカメラフレーム。画面に写る範囲の指定)
    void updateCameraPanel();
    // カメラパネルの現在の表示値からキャンバスのオーバーレイ矩形を更新する
    void updateCameraOverlay();

    // 撮影ウィンドウ(カットのエフェクトスタック+撮影シート)
    void openShootingWindow();
    // エフェクトやカット構成が変わった後に呼ぶ。撮影ウィンドウが開いていれば最新化する
    void refreshShootingWindowIfOpen();

    // カット管理操作(カットバー)
    void setupCutBar();
    void updateCutBar();
    void addCut();
    void removeActiveCut();
    void renameActiveCut();
    void setActiveCut(int index);

    // セル(Xsheetの列)管理操作
    void addCel();
    void removeActiveCel();
    void renameActiveCel();
    void moveActiveCel(int delta);
    // セルの可視状態を指定値に設定する(XsheetパネルのトグルとCelPanelのチェックボックス両方から使う)
    void setCelVisibility(int celIndex, bool visible);
    // アクティブセルを切り替える(CelPanelでのセル選択・Xsheetのセルクリックで共通利用)
    void setActiveCel(int celIndex);
    // 引きセル: アクティブセルの用紙サイズ変更ダイアログを開く(CelPanelの「セルサイズ...」ボタンから)
    void openCelSizeDialog();
    // プロジェクトのキャンバス解像度・アスペクト比を変更するダイアログを開く(ファイルメニューから)
    void openCanvasSizeDialog();

    // 下敷き(参照画像/連番シーケンス)。セッション限定でプロジェクトには保存しない
    void openUnderlay();
    void clearUnderlaySequence();
    void setUnderlaySequenceFromFile(const QString& path);
    void updateUnderlay();

    // ブラシ設定UI
    void choosePenColor();
    void updatePenColorButton();

    // 自動保存・クラッシュリカバリ
    QString autosavePath() const;
    bool performAutosave();

    // プリビズ
    void openPrevizWindow();
    void setPrevizUnderlay(bool enabled);

    // 絵コンテ
    void openStoryboardWindow();

    // 設定ボード
    void openSettingBoardWindow();
    // 参照ドック(ReferencePanel)の内容をプロジェクトの設定ボードから再構築する
    void updateReferencePanel();

    // 編集(カッティング)
    void openEditWindow();
    // カット追加/削除/改名/入れ替えなど構成が変わる操作の後に呼ぶ。編集ウィンドウが
    // 開いていれば一覧・プレビューを最新化する
    void refreshEditWindowIfOpen();

    // プロジェクト管理ウィンドウ(進行管理表+カット構成)
    void openProjectManagerWindow();

    // 書き出し
    void openExportDialog();
    // 書き出す1コマの参照(どのカットのどのコマか)。現在カット/全カット通しを同じ経路で扱う
    struct ExportFrameRef {
        core::Cut* cut;
        size_t frame;
    };
    // frames順に連番PNG(frame_0001.png…)をdirへ書き出す
    bool exportFramesToDir(const QString& dir, const std::vector<ExportFrameRef>& frames,
                           const core::RenderOptions& opts, int outW, int outH, bool includeDrawing,
                           bool includePreviz);
    // frames順に一時連番PNG→ffmpegでmp4へエンコードする
    bool exportMovieFrames(const QString& mp4Path, const std::vector<ExportFrameRef>& frames, int fps,
                           const core::RenderOptions& opts, int outW, int outH, bool includeDrawing,
                           bool includePreviz);
    // 1コマ分の最終画像(作画/プリビズ/両方)をoutW×outHで作る。書き出しの共通描画
    QImage renderExportFrameImage(core::Cut& cut, size_t frame, int outW, int outH, const core::RenderOptions& opts,
                                  bool includeDrawing, bool includePreviz);
    // カットのプリビズ(3Dレイアウト)を指定コマでoutW×outHのQImageへレンダリングする
    QImage renderPrevizExportImage(core::Cut& cut, size_t frame, int outW, int outH);

    std::unique_ptr<core::Project> m_project;
    core::CommandStack m_commands;
    GLCanvas* m_canvas = nullptr;

    size_t m_currentFrame = 0;
    size_t m_activeCut = 0;    // 編集対象のカットインデックス
    size_t m_activeCel = 0;    // 編集対象のセルインデックス
    size_t m_activeLayer = 0;  // 編集対象のレイヤーインデックス(アクティブセル内)
    bool m_onionEnabled = true;
    bool m_playing = false;
    bool m_cleanView = false;  // 仕上げ表示(色トレス線・作監修正を隠す最終画プレビュー)

    QTimer* m_playTimer = nullptr;
    QLabel* m_frameLabel = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_onionAction = nullptr;
    QAction* m_pressureAction = nullptr;   // 表示メニューの筆圧検知トグル(ツールバーの筆圧チェックと同期)
    QCheckBox* m_pressureCheck = nullptr;  // ツールバーの筆圧チェック
    QComboBox* m_cutCombo = nullptr;  // カットバーのカット選択
    FramePanel* m_framePanel = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    PalettePanel* m_palettePanel = nullptr;
    XsheetPanel* m_xsheetPanel = nullptr;
    CelPanel* m_celPanel = nullptr;
    TapPanel* m_tapPanel = nullptr;
    CameraPanel* m_cameraPanel = nullptr;
    PrevizWindow* m_previzWindow = nullptr;  // 別ウィンドウ(遅延生成)
    StoryboardWindow* m_storyboardWindow = nullptr;  // 絵コンテウィンドウ(別ウィンドウ、遅延生成)
    SettingBoardWindow* m_settingBoardWindow = nullptr;  // 設定ボードウィンドウ(別ウィンドウ、遅延生成)
    EditWindow* m_editWindow = nullptr;  // 編集(カッティング)ウィンドウ(別ウィンドウ、遅延生成)
    ProjectManagerWindow* m_projectManagerWindow = nullptr;  // プロジェクト管理ウィンドウ(別ウィンドウ、遅延生成)
    ShootingWindow* m_shootingWindow = nullptr;  // 撮影ウィンドウ(別ウィンドウ、遅延生成)
    ReferencePanel* m_referencePanel = nullptr;  // 設定ボード参照ドック
    int m_referenceBoardIndex = -1;  // 参照ドックで選択中の設定ボードインデックス(未選択-1)
    QString m_currentFilePath;
    QString m_lastExportPath;  // 前回の書き出し先(書き出しダイアログの出力先を復元する)
    bool m_dirty = false;  // 未保存の変更があるか(表示用。詳細な範囲はm_dirtyScopeが持つ)

    // 保存スコープ: どのファイルを書き直す必要があるか(部分保存)。安全側に倒す(迷ったら全部)
    struct DirtyScope {
        bool project = false;     // project.ppam(構造+パレット)
        bool storyboard = false;  // storyboard.ppam
        bool boards = false;      // boards.ppam
        bool allCuts = false;     // trueなら全カット書き出し(cutIdsは無視)
        std::set<uint64_t> cutIds;  // allCuts=falseのとき書き出す個別カットのID

        void clear() {
            project = storyboard = boards = allCuts = false;
            cutIds.clear();
        }
    };
    DirtyScope m_dirtyScope;

    // 移動ツール(タップ/ペグ移動)のドラッグ状態: ドラッグ開始時点のアクティブセル位置(補間値)
    QPointF m_moveBase;

    // 自動保存
    QTimer* m_autosaveTimer = nullptr;

    // ブラシ設定UI(太さスライダー・色選択ボタン)
    QSlider* m_penRadiusSlider = nullptr;
    QLabel* m_penRadiusValueLabel = nullptr;
    QToolButton* m_penColorButton = nullptr;
    QColor m_penColor = Qt::black;
    // 太さスライダーはツールごとに値を記憶する(ペン/塗りつぶしはペンの値を共有、消しゴムは専用)
    int m_penRadiusValue = 6;
    int m_eraserRadiusValue = 24;

    // プリビズを下敷きにするか(有効時は連番下敷きより優先)
    bool m_previzUnderlay = false;

    // 下敷き(参照画像/連番シーケンス): 選択フォルダ内の同拡張子ファイル一覧(名前順)
    QStringList m_underlaySequence;
    int m_underlayLoadedIndex = -1;  // 直前にロードした連番のインデックス(再ロード抑止用)
};
