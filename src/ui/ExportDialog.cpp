#include "ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int kDefaultFps = 24;
}  // namespace

ExportDialog::ExportDialog(const QStringList& celNames, int frameCount, QWidget* parent)
    : QDialog(parent), m_frameCount(std::max(1, frameCount)) {
    setWindowTitle(tr("書き出し"));

    auto* formLayout = new QFormLayout();

    // 形式: mp4はffmpeg.exeが見つからない場合は無効化する
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("連番PNG"));
    m_formatCombo->addItem(tr("動画(mp4)"));
    const QString ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpegPath.isEmpty()) {
        const int movieIndex = 1;
        // モデルのアイテムフラグからEnabledを外して選択不可にし、ツールチップで理由を案内する
        if (auto* model = qobject_cast<QStandardItemModel*>(m_formatCombo->model())) {
            if (QStandardItem* item = model->item(movieIndex)) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
        }
        m_formatCombo->setItemData(movieIndex, tr("ffmpeg.exeが見つかりません。PATHに追加してください。"), Qt::ToolTipRole);
    }
    formLayout->addRow(tr("形式:"), m_formatCombo);

    // 含める内容(作画/プリビズ/両方)。書き出しに何を入れるかを選ぶ
    m_contentCombo = new QComboBox(this);
    m_contentCombo->addItem(tr("作画のみ"));
    m_contentCombo->addItem(tr("プリビズのみ"));
    m_contentCombo->addItem(tr("作画+プリビズ"));
    formLayout->addRow(tr("含める内容:"), m_contentCombo);

    // 出力先: 形式に応じて参照ボタンの動作(フォルダ選択/保存ファイル名)を切り替える
    auto* outputRow = new QWidget(this);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    m_outputPathEdit = new QLineEdit(outputRow);
    auto* browseButton = new QPushButton(tr("参照..."), outputRow);
    outputLayout->addWidget(m_outputPathEdit);
    outputLayout->addWidget(browseButton);
    formLayout->addRow(tr("出力先:"), outputRow);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browseOutputPath);

    // 範囲: 開始/終了コマ(既定は1〜尺)
    auto* rangeRow = new QWidget(this);
    auto* rangeLayout = new QHBoxLayout(rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    m_fromSpin = new QSpinBox(rangeRow);
    m_fromSpin->setRange(1, m_frameCount);
    m_fromSpin->setValue(1);
    m_toSpin = new QSpinBox(rangeRow);
    m_toSpin->setRange(1, m_frameCount);
    m_toSpin->setValue(m_frameCount);
    rangeLayout->addWidget(m_fromSpin);
    rangeLayout->addWidget(new QLabel(tr("〜"), rangeRow));
    rangeLayout->addWidget(m_toSpin);
    formLayout->addRow(tr("範囲:"), rangeRow);
    // 開始/終了が逆転しないよう、変更時に相手側を追従させる
    connect(m_fromSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (value > m_toSpin->value()) m_toSpin->setValue(value);
    });
    connect(m_toSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (value < m_fromSpin->value()) m_fromSpin->setValue(value);
    });

    // 対象: 全セル(仕上げ)+各セル名
    m_celCombo = new QComboBox(this);
    m_celCombo->addItem(tr("全セル(仕上げ)"));
    for (const QString& name : celNames) {
        m_celCombo->addItem(tr("セル %1 のみ").arg(name));
    }
    formLayout->addRow(tr("対象:"), m_celCombo);

    // チェック: 色トレス線・作監修正の含有(既定オフ)
    m_colorTraceCheck = new QCheckBox(tr("色トレス線を含める"), this);
    formLayout->addRow(QString(), m_colorTraceCheck);
    m_correctionCheck = new QCheckBox(tr("作監修正を含める"), this);
    formLayout->addRow(QString(), m_correctionCheck);

    // 透明背景: 連番PNG+作画のみで有効(アルファ付きPNG)
    m_transparentCheck = new QCheckBox(tr("背景を透明にする(連番PNG・作画のみ)"), this);
    formLayout->addRow(QString(), m_transparentCheck);

    // 出力解像度スケール(%)。100=キャンバス等倍
    m_scaleSpin = new QSpinBox(this);
    m_scaleSpin->setRange(10, 400);
    m_scaleSpin->setValue(100);
    m_scaleSpin->setSuffix(tr(" %"));
    formLayout->addRow(tr("出力スケール:"), m_scaleSpin);

    // FPS: mp4書き出し時のみ有効
    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(1, 60);
    m_fpsSpin->setValue(kDefaultFps);
    formLayout->addRow(tr("FPS:"), m_fpsSpin);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("書き出し"));
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_buttonBox);

    connect(m_formatCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateFormatDependentUi(); });
    connect(m_contentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateFormatDependentUi(); });
    updateFormatDependentUi();
}

void ExportDialog::updateFormatDependentUi() {
    const bool isMovie = format() == Format::Movie;
    m_fpsSpin->setEnabled(isMovie);

    const bool drawingOnly = content() == Content::Drawing;
    // 対象セル・色トレス/作監修正は「作画」を含むときだけ意味がある
    const bool hasDrawing = content() != Content::Previz;
    m_celCombo->setEnabled(hasDrawing);
    m_colorTraceCheck->setEnabled(hasDrawing);
    m_correctionCheck->setEnabled(hasDrawing);
    // 透明背景は 連番PNG かつ 作画のみ のときだけ有効(動画・プリビズ含みは不透明)
    m_transparentCheck->setEnabled(!isMovie && drawingOnly);
}

void ExportDialog::browseOutputPath() {
    if (format() == Format::Sequence) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("出力先フォルダを選択"), m_outputPathEdit->text());
        if (!dir.isEmpty()) m_outputPathEdit->setText(dir);
    } else {
        const QString path = QFileDialog::getSaveFileName(this, tr("出力先ファイルを選択"), m_outputPathEdit->text(),
                                                            tr("MP4動画 (*.mp4)"));
        if (!path.isEmpty()) m_outputPathEdit->setText(path);
    }
}

ExportDialog::Format ExportDialog::format() const {
    return m_formatCombo->currentIndex() == 1 ? Format::Movie : Format::Sequence;
}

ExportDialog::Content ExportDialog::content() const {
    switch (m_contentCombo->currentIndex()) {
        case 1:
            return Content::Previz;
        case 2:
            return Content::Both;
        default:
            return Content::Drawing;
    }
}

QString ExportDialog::outputPath() const {
    return m_outputPathEdit->text();
}

int ExportDialog::fromFrame() const {
    return m_fromSpin->value();
}

int ExportDialog::toFrame() const {
    return m_toSpin->value();
}

int ExportDialog::onlyCel() const {
    return m_celCombo->currentIndex() - 1;  // 0番目が「全セル」なので-1にずらす
}

bool ExportDialog::includeColorTrace() const {
    return m_colorTraceCheck->isChecked();
}

bool ExportDialog::includeCorrection() const {
    return m_correctionCheck->isChecked();
}

bool ExportDialog::transparentBackground() const {
    // UIで無効化されている条件(動画/プリビズ含み)では常にfalse
    return m_transparentCheck->isEnabled() && m_transparentCheck->isChecked();
}

int ExportDialog::outputScalePercent() const {
    return m_scaleSpin->value();
}

int ExportDialog::fps() const {
    return m_fpsSpin->value();
}
