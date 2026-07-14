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

    // 書き出しに含める内容(エアラベル=選択式)。作画(セル合成)/プリビズ(3Dレイアウト)/両方
    enum class Content {
        Drawing,  // 作画のみ(従来)
        Previz,   // プリビズのみ(3Dレイアウト)
        Both,     // 作画+プリビズ(プリビズを背景に作画を重ねる)
    };

    // celNames: 対象セルの一覧(「全セル(仕上げ)」の下に「セル <name> のみ」として並べる)
    // frameCount: カットの尺(開始/終了コマの範囲既定値・上限に使う)
    ExportDialog(const QStringList& celNames, int frameCount, QWidget* parent = nullptr);

    Format format() const;
    Content content() const;
    // trueなら全カットを通しで書き出す(範囲指定は無視)。falseなら現在のカットのfrom〜to
    bool exportAllCuts() const;
    QString outputPath() const;
    int fromFrame() const;
    int toFrame() const;
    // -1=全セル、0以上=対象セルのインデックス
    int onlyCel() const;
    bool includeColorTrace() const;
    bool includeCorrection() const;
    // 透明背景で書き出す(連番PNG+作画のみで有効。動画やプリビズ含む書き出しでは無視)
    bool transparentBackground() const;
    // 出力解像度スケール(%)。100=キャンバス等倍
    int outputScalePercent() const;
    int fps() const;

private:
    void updateFormatDependentUi();
    void browseOutputPath();

    int m_frameCount = 1;

    QComboBox* m_formatCombo = nullptr;
    QComboBox* m_contentCombo = nullptr;
    QComboBox* m_scopeCombo = nullptr;
    QLineEdit* m_outputPathEdit = nullptr;
    QSpinBox* m_fromSpin = nullptr;
    QSpinBox* m_toSpin = nullptr;
    QComboBox* m_celCombo = nullptr;
    QCheckBox* m_colorTraceCheck = nullptr;
    QCheckBox* m_correctionCheck = nullptr;
    QCheckBox* m_transparentCheck = nullptr;
    QSpinBox* m_scaleSpin = nullptr;
    QSpinBox* m_fpsSpin = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};
