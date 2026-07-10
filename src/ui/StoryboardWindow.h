#pragma once

#include <QColor>
#include <QMainWindow>

class QTableWidgetItem;
class QTableWidget;
class QLabel;
class QPushButton;
class QSlider;
class QPlainTextEdit;
class GLCanvas;

namespace core {
class Project;
}

// 絵コンテウィンドウ(別ウィンドウ)。絵コンテは全工程の前に単体で描くもの:
// 独立した「パネル(コマ)」の列で、パネルごとにラフ絵を手描きする。
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
    // 内容欄/セリフ欄キャンバスのストローク完了(サムネ更新はせず編集通知のみ)
    void onOverlayStrokeFinished();
    void updateTotalDurationLabel();
    void updateThumbnail(int row);
    // 選択中パネルのdrawing/actionDrawing/dialogueDrawingへ描画エリアを再設定する(vectorの再配置で
    // ポインタが無効になるため、パネル追加/削除/並べ替えの後は必ず呼ぶこと)
    void bindCanvasToSelectedPanel();
    // 太さスライダーの値を選択中ツール(ペン/消しゴム)の半径へ反映する(3キャンバスへ同時適用)
    void onRadiusSliderChanged(int value);
    // 色選択ダイアログを開き、選択色を3キャンバスのペン色へ反映する
    void chooseColor();
    // 現在の太さ/色設定を3キャンバスへ適用する
    void applyToolSettingsToCanvases();
    // 内容/セリフ欄(複数行テキスト)を選択パネルへ反映し、対応キャンバスのテキスト下敷きを更新する
    void onActionTextChanged();
    void onDialogueTextChanged();
    int selectedPanelIndex() const;

    core::Project* m_project = nullptr;
    QTableWidget* m_table = nullptr;
    GLCanvas* m_canvas = nullptr;
    GLCanvas* m_actionCanvas = nullptr;    // 内容欄キャンバス(テキストの上に手書き)
    GLCanvas* m_dialogueCanvas = nullptr;  // セリフ欄キャンバス(テキストの上に手書き)
    QLabel* m_totalLabel = nullptr;
    QPushButton* m_penButton = nullptr;
    QPushButton* m_eraserButton = nullptr;
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
};
