#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QDialogButtonBox;

// 新規プロジェクト作成ダイアログ。プロジェクト名・キャンバス解像度(プリセット/カスタム)・
// FPSなど、作成にあたって必要な項目をまとめて決める。プリセット選択で幅/高さを自動セットし、
// スピンを手編集するとプリセットは「カスタム」へ切り替わる。
class NewProjectDialog : public QDialog {
    Q_OBJECT

public:
    // defaultFps: 現在の既定FPS(通常24)。名前/解像度は標準値で初期化する
    explicit NewProjectDialog(int defaultFps = 24, QWidget* parent = nullptr);

    QString projectName() const;
    int canvasWidth() const;
    int canvasHeight() const;
    int fps() const;

private:
    void applyPreset(int index);
    void onSpinEdited();
    void updateAspectLabel();

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QLabel* m_aspectLabel = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    bool m_updatingFromPreset = false;
};
