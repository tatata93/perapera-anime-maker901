#pragma once

#include <QColor>
#include <QMainWindow>

#include "core/CommandStack.h"

class QAction;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QLabel;
class QString;
class QVBoxLayout;
class QWidget;
class FloatingCanvasWindow;
class GLCanvas;
class LayerPanel;

namespace core {
class Project;
enum class LayerRole;
}

// 設定ボードウィンドウ(別ウィンドウ)。キャラクター設定・美術設定などの資料を
// 「描く/画像を貼る」ボードとして扱う。複数枚を名前付きで管理し、カット/シーンとは
// 独立してプロジェクト直下に保存される。作画中はメインウィンドウの参照ドック
// (ReferencePanel)でいつでも内容を見られる
class SettingBoardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SettingBoardWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // ボード一覧を再構築する(m_updatingガードで選択変更シグナルの暴発を防ぐ)。
    // vector再配置に備え、描画エリアも選択ボードへ再設定する
    void refresh();
    void debugDetachCanvas();
    FloatingCanvasWindow* debugFloatingCanvasWindow() const;
    bool debugExportSelectedBoardImage(const QString& path);

signals:
    void edited();  // ボードの追加/削除/名前変更/手描き/画像貼付が行われた

private:
    void addBoard();
    void removeBoard();
    void renameBoard();
    void onSelectionChanged();
    void onStrokeFinished();
    // 「画像を貼る」ボタン: 画像ファイルを選び、選択中ボードへアスペクト維持で最大フィット
    // 縮小・中央配置してsrc-over合成する
    void pasteImage();
    void resizeBoardCanvas();
    void exportBoardImage();
    void editTextBoxes();
    void toggleFinalStamp(bool checked);
    void updateFinalStampOverlay();
    void detachCanvas();
    void restoreCanvas();
    // 選択中ボードのimage(ボード全体のビットマップ)へ描画エリアを再設定する(vectorの
    // 再配置でポインタが無効になるため、ボード追加/削除/並べ替えの後は必ず呼ぶこと)
    void bindCanvasToSelectedBoard();
    // 太さスライダーの値を選択中ツール(ペン/消しゴム)の半径へ反映する
    void onRadiusSliderChanged(int value);
    // 色選択ダイアログを開き、選択色をキャンバスのペン色へ反映する
    void chooseColor();
    // 現在の太さ/色設定をキャンバスへ適用する
    void applyToolSettingsToCanvas();
    GLCanvas* createCanvas(QWidget* parent);
    QWidget* createFloatingCanvasPanel(QWidget* parent);
    void setActiveTool(int tool);
    void undo();
    void redo();
    void updateUndoActions();
    void clearUndoHistory();
    int selectedBoardIndex() const;
    void refreshLayerPanel();
    void addPaintLayer(core::LayerRole role);
    void duplicatePaintLayer(int layerIndex);
    void removePaintLayer();
    void movePaintLayer(int delta);
    void renamePaintLayer(int layerIndex);
    void setPaintLayerRole(int layerIndex, core::LayerRole role);
    void syncSelectedBoardComposite();

    // 色指定(色指定書)操作: 選択中ボードのcolorSpecsを編集する
    void addColorSpec();       // 色を選び、名前を付けて追加する
    void renameColorSpec();    // 選択中の色指定の名前を変更する
    void changeColorSpecColor();  // 選択中の色指定の色を変更する
    void removeColorSpec();    // 選択中の色指定を削除する
    // 色指定リストの行をダブルクリック: その色を現在のペン色に設定する
    void onColorSpecActivated(QListWidgetItem* item);
    // 選択中ボードのcolorSpecsから色指定リストを再構築する(m_updatingガード)
    void refreshColorSpecList();
    int selectedColorSpecIndex() const;

    core::Project* m_project = nullptr;
    QListWidget* m_list = nullptr;
    GLCanvas* m_canvas = nullptr;  // 選択中ボード1枚(1920x1080)を表示・編集するキャンバス
    QWidget* m_canvasHost = nullptr;
    QVBoxLayout* m_canvasLayout = nullptr;
    QPushButton* m_penButton = nullptr;
    QPushButton* m_eraserButton = nullptr;
    QPushButton* m_fillButton = nullptr;
    QPushButton* m_lassoButton = nullptr;
    QPushButton* m_eyedropperButton = nullptr;
    QPushButton* m_finalStampButton = nullptr;
    QSlider* m_radiusSlider = nullptr;
    QLabel* m_radiusValueLabel = nullptr;
    QPushButton* m_colorButton = nullptr;
    QListWidget* m_colorSpecList = nullptr;  // 色指定(色指定書)一覧: スウォッチ+名前
    LayerPanel* m_layerPanel = nullptr;
    FloatingCanvasWindow* m_floatingCanvasWindow = nullptr;
    bool m_updating = false;
    int m_selectedRow = -1;  // 現在選択中のボード行(ボードが1枚もなければ-1)

    // ペン/消しゴムそれぞれの太さ・色設定(トグル切替時にスライダー表示を切り替えるため記憶する)
    float m_penRadius = 6.0f;
    float m_eraserRadius = 24.0f;
    QColor m_penColor = Qt::black;
    core::CommandStack m_commands;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
};
