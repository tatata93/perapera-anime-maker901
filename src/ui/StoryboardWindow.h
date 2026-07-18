#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointF>
#include <memory>

class QTableWidgetItem;
class QTableWidget;
class QLabel;
class QPushButton;
class QSlider;
class QPlainTextEdit;
class QDialog;
class QVBoxLayout;
class QWidget;
class FloatingCanvasWindow;
class GLCanvas;

namespace core {
class Project;
class Command;
}

// 絵コンテウィンドウ(別ウィンドウ)。絵コンテは全工程の前に単体で描くもの:
// 独立した「パネル(コマ)」の列で、パネルごとに「よくあるコンテ用紙」風の1枚(罫線・見出し・
// カット番号・絵の枠・内容欄・セリフ欄・秒欄を印字した紙)にラフ絵/手書きメモを描く。
// 紙全体を1つの手書きビットマップが覆うため、絵の枠内はもちろん内容欄への効果音メモや
// 枠をまたぐ矢印なども自由に手描きできる。
// 同じカット番号を複数パネルに書けば「1カット複数コマ」のコンテになる。
class StoryboardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit StoryboardWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // 全行を再構築し、サムネイルを撮り直す(m_updatingガードで編集シグナルの暴発を防ぐ)。
    // vector再配置に備え、描画エリアも選択パネルへ再設定する
    void refresh();

    // 動作確認用: 絵の枠のダブルクリックと同じ効果(拡大トグルON)を「絵を拡大」ボタン経由で起こす
    void debugZoomToFrame();
    // 動作確認用: プレビュー(ビデオコンテ)ダイアログを開き自動再生を開始する
    void debugOpenPreview();
    // 動作確認用: 現在開いているプレビューダイアログ(未使用時はnullptr)
    QDialog* debugPreviewDialog() const { return m_previewDialog; }
    // 動作確認用: キャンバスを別窓へ移す
    void debugDetachCanvas();
    FloatingCanvasWindow* debugFloatingCanvasWindow() const;

signals:
    void edited();  // カット番号/内容/セリフ/尺またはコンテ絵が編集された
    // 「パネルからカット作成」。cutLabelは選択パネルのカット番号、totalFramesは
    // 同じカット番号を持つ全パネルのduration合計
    void createCutRequested(QString cutLabel, int totalFrames);

private:
    void onItemChanged(QTableWidgetItem* item);
    void onSelectionChanged();
    void addPanel();
    void removePanel();
    void movePanel(int delta);
    void createCutFromPanel();
    void onStrokeFinished();
    void updateTotalDurationLabel();
    void updateThumbnail(int row);
    // 選択中パネルのdrawing(コンテ用紙全体への手書きインク)へ描画エリアを再設定し、コンテ用紙の
    // 下敷きも敷き直す(vectorの再配置でポインタが無効になるため、パネル追加/削除/並べ替えの
    // 後は必ず呼ぶこと)
    void bindCanvasToSelectedPanel();
    // 太さスライダーの値を選択中ツール(ペン/消しゴム)の半径へ反映する
    void onRadiusSliderChanged(int value);
    // 色選択ダイアログを開き、選択色をキャンバスのペン色へ反映する
    void chooseColor();
    // 現在の太さ/色設定をキャンバスへ適用する
    void applyToolSettingsToCanvases();
    GLCanvas* createCanvas(QWidget* parent);
    // 内容/セリフ欄(複数行テキスト)を選択パネルへ反映し、コンテ用紙下敷きを最新テキストで敷き直す
    void onActionTextChanged();
    void onDialogueTextChanged();
    int selectedPanelIndex() const;
    // 絵の枠(kFrameRect)のダブルクリックで拡大/解除をトグルする。直前クリックで打たれた点があれば
    // undoしてから判定する(imagePosは画像座標)
    void onCanvasDoubleClicked(QPointF imagePos);
    // プレビュー(ビデオコンテ)ダイアログを開く(既に開いていれば前面へ)
    void openPreview();
    void detachCanvas();
    void restoreCanvas();

    core::Project* m_project = nullptr;
    QTableWidget* m_table = nullptr;
    GLCanvas* m_canvas = nullptr;  // コンテ用紙1枚(下敷き+手書きインク)を表示・編集するキャンバス
    QWidget* m_canvasHost = nullptr;
    QVBoxLayout* m_canvasLayout = nullptr;
    QLabel* m_totalLabel = nullptr;
    QPushButton* m_penButton = nullptr;
    QPushButton* m_eraserButton = nullptr;
    QPushButton* m_eyedropperButton = nullptr;
    QSlider* m_radiusSlider = nullptr;
    QLabel* m_radiusValueLabel = nullptr;
    QPushButton* m_colorButton = nullptr;
    QPlainTextEdit* m_actionEdit = nullptr;
    QPlainTextEdit* m_dialogueEdit = nullptr;
    bool m_updating = false;
    int m_selectedRow = -1;  // 現在選択中のパネル行(パネルが1枚もなければ-1)

    // ペン/消しゴムそれぞれの太さ・色設定(トグル切替時にスライダー表示を切り替えるため記憶する)
    float m_penRadius = 6.0f;
    float m_eraserRadius = 24.0f;
    QColor m_penColor = Qt::black;

    QPushButton* m_zoomButton = nullptr;  // 「絵を拡大」チェック可能ボタン(絵の枠の拡大/解除)
    bool m_frameZoomed = false;           // 絵の枠を拡大表示中か(パネル切替時も維持する)

    // 直近のストロークコマンドと受領時刻。ダブルクリックの1回目で打たれてしまった点を、
    // ダブルクリック検出時に取り消す(undo)ために保持する
    std::unique_ptr<core::Command> m_lastStroke;
    QElapsedTimer m_lastStrokeTimer;

    QDialog* m_previewDialog = nullptr;  // プレビュー(ビデオコンテ再生)ダイアログ(未使用時はnullptr)
    FloatingCanvasWindow* m_floatingCanvasWindow = nullptr;
};
