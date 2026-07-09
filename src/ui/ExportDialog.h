#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QDialogButtonBox;

// 書き出し設定ダイアログ。形式(連番PNG/動画mp4)・出力先・範囲・対象セル・
// 色トレス線/作監修正の含有・FPSを入力し、OKで確定した内容をgetterで取得する。
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Format {
        Sequence,  // 連番PNG
        Movie,     // 動画(mp4)
    };

    // celNames: 対象セルの一覧(「全セル(仕上げ)」の下に「セル <name> のみ」として並べる)
    // frameCount: カットの尺(開始/終了コマの範囲既定値・上限に使う)
    ExportDialog(const QStringList& celNames, int frameCount, QWidget* parent = nullptr);

    Format format() const;
    QString outputPath() const;
    int fromFrame() const;
    int toFrame() const;
    // -1=全セル、0以上=対象セルのインデックス
    int onlyCel() const;
    bool includeColorTrace() const;
    bool includeCorrection() const;
    int fps() const;

private:
    void updateFormatDependentUi();
    void browseOutputPath();

    int m_frameCount = 1;

    QComboBox* m_formatCombo = nullptr;
    QLineEdit* m_outputPathEdit = nullptr;
    QSpinBox* m_fromSpin = nullptr;
    QSpinBox* m_toSpin = nullptr;
    QComboBox* m_celCombo = nullptr;
    QCheckBox* m_colorTraceCheck = nullptr;
    QCheckBox* m_correctionCheck = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
