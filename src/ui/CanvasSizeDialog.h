#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
class QLabel;

// プロジェクトのキャンバス解像度(・アスペクト比)を設定するダイアログ。
// プリセット選択で幅/高さスピンを自動セットし、スピンを手編集すると自動的に
// プリセットが「カスタム」へ切り替わる。OK時に反映されるのは「これから描く紙のサイズ」
// (新規セル・合成・書き出し)のみで、既存の作画セルのサイズは変わらない旨を注意書きで示す
class CanvasSizeDialog : public QDialog {
    Q_OBJECT

public:
    // currentW/currentH: 現在のプロジェクトのキャンバスサイズ
    CanvasSizeDialog(int currentW, int currentH, QWidget* parent = nullptr);

    int canvasWidth() const;
    int canvasHeight() const;

private:
    void applyPreset(int index);
    void onSpinEdited();
    void updateAspectLabel();

    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QLabel* m_aspectLabel = nullptr;
    // プリセット側からスピンの値を書き換えている間、onSpinEdited()の
    // 「カスタムへ切替」ロジックを止めるためのガード
    bool m_updatingFromPreset = false;
};
