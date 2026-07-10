#pragma once

#include <QColor>
#include <QMainWindow>
#include <QPointF>
#include <QStringList>
#include <memory>

#include "core/CommandStack.h"
#include "core/Project.h"

class CameraPanel;
class CelPanel;
class FramePanel;
class GLCanvas;
class LayerPanel;
class PalettePanel;
class PrevizWindow;
class StoryboardWindow;
class TapPanel;
class XsheetPanel;
class QCloseEvent;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

namespace core {
struct RenderOptions;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    GLCanvas* canvas() const { return m_canvas; }

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
    int debugPaletteRoundTrip(const QString& ppamPath);
    // レイヤー種別確認用: レイヤーを2枚追加(計3枚)し種別を設定→保存→新規→読込を行い、
    // 往復が正しければ0、不一致なら1を返す
    int debugRoleRoundTrip(const QString& ppamPath);

    // 書き出し確認用: タイムシートデモ(尺6・2コマ打ち)を組んでから全コマを連番PNGで書き出す。
    // 成功時0、失敗時1を返す
    int debugExportSequence(const QString& dir);
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
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    void saveAs();
    void save();
    void open();
    void updateWindowTitle();
    void undo();
    void redo();

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

    // 書き出し
    void openExportDialog();
    bool exportSequence(const QString& dir, int from, int to, const core::RenderOptions& opts);
    bool exportMovie(const QString& mp4Path, int from, int to, int fps, const core::RenderOptions& opts);

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
    QString m_currentFilePath;
    bool m_dirty = false;  // 未保存の変更があるか

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
