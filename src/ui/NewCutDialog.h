#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;

// 新規カット作成ダイアログ。カット名・尺(コマ、fpsから秒を表示)・初期の進捗(7段階)・
// 絵コンテ内容/セリフ・初期セル枚数など、カット作成に必要な項目をまとめて決める。
class NewCutDialog : public QDialog {
    Q_OBJECT

public:
    // suggestedName: 初期のカット名(例「カット 4」)、defaultFps: 秒換算に使うFPS
    NewCutDialog(const QString& suggestedName, int defaultFps = 24, QWidget* parent = nullptr);

    QString cutName() const;
    int frameCount() const;
    int status() const;  // CutStatusのenum値(0..6)
    QString action() const;
    QString dialogue() const;
    int celCount() const;  // 初期セル枚数(1以上)

private:
    void updateSecondsLabel();

    int m_fps = 24;
    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_frameSpin = nullptr;
    QLabel* m_secondsLabel = nullptr;
    QComboBox* m_statusCombo = nullptr;
    QPlainTextEdit* m_actionEdit = nullptr;
    QPlainTextEdit* m_dialogueEdit = nullptr;
    QSpinBox* m_celCountSpin = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
