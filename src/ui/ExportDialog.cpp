#include "ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

using perapera::ui::ExportContent;
using perapera::ui::ExportFormat;
using perapera::ui::ExportScope;
using perapera::ui::ExportSettings;

template <typename Enum>
void addEnumItem(QComboBox* combo, const QString& text, Enum value) {
    combo->addItem(text, static_cast<int>(value));
}

template <typename Enum>
Enum currentEnum(const QComboBox* combo) {
    return static_cast<Enum>(combo->currentData().toInt());
}

bool setComboByData(QComboBox* combo, const QVariant& data) {
    const int index = combo->findData(data);
    if (index < 0) return false;
    combo->setCurrentIndex(index);
    return true;
}

void setItemEnabled(QComboBox* combo, int index, bool enabled, const QString& tooltip = {}) {
    if (auto* model = qobject_cast<QStandardItemModel*>(combo->model())) {
        if (QStandardItem* item = model->item(index)) {
            Qt::ItemFlags flags = item->flags();
            item->setFlags(enabled ? (flags | Qt::ItemIsEnabled) : (flags & ~Qt::ItemIsEnabled));
        }
    }
    combo->setItemData(index, tooltip, Qt::ToolTipRole);
}

}  // namespace

ExportDialog::ExportDialog(const QStringList& celNames, int frameCount, int canvasWidth, int canvasHeight,
                           int currentFrame, QWidget* parent)
    : QDialog(parent),
      m_frameCount(std::max(1, frameCount)),
      m_canvasWidth(std::max(2, canvasWidth)),
      m_canvasHeight(std::max(2, canvasHeight)),
      m_currentFrame(std::clamp(currentFrame, 0, std::max(0, frameCount - 1))) {
    setWindowTitle(tr("書き出し"));
    setMinimumSize(620, 540);
    resize(680, 610);

    auto* tabs = new QTabWidget(this);
    auto* basicPage = new QWidget(tabs);
    auto* basicForm = new QFormLayout(basicPage);
    basicForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_presetCombo = new QComboBox(basicPage);
    m_presetCombo->setObjectName(QStringLiteral("exportPresetCombo"));
    m_presetCombo->addItem(tr("確認用 MP4"));
    m_presetCombo->addItem(tr("編集用 ProRes 422 HQ"));
    m_presetCombo->addItem(tr("編集用 DNxHR HQX"));
    m_presetCombo->addItem(tr("透過編集用 ProRes 4444"));
    m_presetCombo->addItem(tr("仕上げ用 PNG連番"));
    m_presetCombo->addItem(tr("合成用 OpenEXR連番"));
    m_presetCombo->addItem(tr("自由設定"));
    basicForm->addRow(tr("用途:"), m_presetCombo);

    m_formatCombo = new QComboBox(basicPage);
    m_formatCombo->setObjectName(QStringLiteral("exportFormatCombo"));
    addEnumItem(m_formatCombo, tr("PNG連番（仕上げ・透過）"), ExportFormat::PngSequence);
    addEnumItem(m_formatCombo, tr("TIFF連番（可逆）"), ExportFormat::TiffSequence);
    addEnumItem(m_formatCombo, tr("JPEG連番（確認用）"), ExportFormat::JpegSequence);
    addEnumItem(m_formatCombo, tr("OpenEXR連番（合成用）"), ExportFormat::OpenExrSequence);
    addEnumItem(m_formatCombo, tr("DPX連番（撮影・DI用）"), ExportFormat::DpxSequence);
    addEnumItem(m_formatCombo, tr("MP4 / H.264（確認・共有用）"), ExportFormat::Mp4H264);
    addEnumItem(m_formatCombo, tr("MOV / ProRes（編集用）"), ExportFormat::MovProRes);
    addEnumItem(m_formatCombo, tr("MOV / DNxHR（編集用）"), ExportFormat::MovDnxhr);
    basicForm->addRow(tr("形式:"), m_formatCombo);

    const bool hasFfmpeg = !perapera::ui::findFfmpegExecutable().isEmpty();
    if (!hasFfmpeg) {
        const QString tooltip = tr("ffmpeg.exeが見つかりません。アプリと同じフォルダかPATHに置いてください。");
        for (int i = 0; i < m_formatCombo->count(); ++i) {
            const auto itemFormat = static_cast<ExportFormat>(m_formatCombo->itemData(i).toInt());
            if (perapera::ui::requiresFfmpeg(itemFormat)) setItemEnabled(m_formatCombo, i, false, tooltip);
        }
        for (int i = 0; i <= 5; ++i) setItemEnabled(m_presetCombo, i, i == 4, tooltip);
    }

    m_scopeCombo = new QComboBox(basicPage);
    addEnumItem(m_scopeCombo, tr("選択中の1コマ"), ExportScope::CurrentFrame);
    addEnumItem(m_scopeCombo, tr("現在のカット・範囲指定"), ExportScope::CurrentCutRange);
    addEnumItem(m_scopeCombo, tr("現在のカット・全コマ"), ExportScope::CurrentCut);
    addEnumItem(m_scopeCombo, tr("全カット通し"), ExportScope::AllCuts);
    setComboByData(m_scopeCombo, static_cast<int>(ExportScope::CurrentCut));
    basicForm->addRow(tr("書き出し対象:"), m_scopeCombo);

    auto* rangeRow = new QWidget(basicPage);
    auto* rangeLayout = new QHBoxLayout(rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    m_fromSpin = new QSpinBox(rangeRow);
    m_fromSpin->setRange(1, m_frameCount);
    m_fromSpin->setValue(1);
    m_toSpin = new QSpinBox(rangeRow);
    m_toSpin->setRange(1, m_frameCount);
    m_toSpin->setValue(m_frameCount);
    rangeLayout->addWidget(m_fromSpin);
    rangeLayout->addWidget(new QLabel(tr("～"), rangeRow));
    rangeLayout->addWidget(m_toSpin);
    rangeLayout->addStretch();
    basicForm->addRow(tr("コマ範囲:"), rangeRow);

    m_contentCombo = new QComboBox(basicPage);
    addEnumItem(m_contentCombo, tr("作画のみ"), ExportContent::Drawing);
    addEnumItem(m_contentCombo, tr("プリビズのみ"), ExportContent::Previz);
    addEnumItem(m_contentCombo, tr("作画＋プリビズ"), ExportContent::Both);
    basicForm->addRow(tr("内容:"), m_contentCombo);

    m_celCombo = new QComboBox(basicPage);
    m_celCombo->addItem(tr("全セル（仕上げ）"));
    for (const QString& name : celNames) m_celCombo->addItem(tr("セル %1 のみ").arg(name));
    basicForm->addRow(tr("セル:"), m_celCombo);

    auto* outputRow = new QWidget(basicPage);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    m_outputPathEdit = new QLineEdit(outputRow);
    m_outputPathEdit->setObjectName(QStringLiteral("exportOutputPathEdit"));
    auto* browseButton = new QPushButton(tr("参照..."), outputRow);
    outputLayout->addWidget(m_outputPathEdit, 1);
    outputLayout->addWidget(browseButton);
    basicForm->addRow(tr("出力先:"), outputRow);

    m_resolutionCombo = new QComboBox(basicPage);
    addEnumItem(m_resolutionCombo, tr("キャンバス等倍（%1×%2）").arg(m_canvasWidth).arg(m_canvasHeight),
                ResolutionPreset::Canvas);
    addEnumItem(m_resolutionCombo, tr("50%"), ResolutionPreset::Half);
    addEnumItem(m_resolutionCombo, tr("200%"), ResolutionPreset::Double);
    addEnumItem(m_resolutionCombo, tr("HD（1280×720）"), ResolutionPreset::Hd);
    addEnumItem(m_resolutionCombo, tr("Full HD（1920×1080）"), ResolutionPreset::FullHd);
    addEnumItem(m_resolutionCombo, tr("UHD 4K（3840×2160）"), ResolutionPreset::Uhd);
    addEnumItem(m_resolutionCombo, tr("任意サイズ"), ResolutionPreset::Custom);
    basicForm->addRow(tr("解像度:"), m_resolutionCombo);

    auto* sizeRow = new QWidget(basicPage);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    m_widthSpin = new QSpinBox(sizeRow);
    m_widthSpin->setRange(16, 16384);
    m_heightSpin = new QSpinBox(sizeRow);
    m_heightSpin->setRange(16, 16384);
    sizeLayout->addWidget(m_widthSpin);
    sizeLayout->addWidget(new QLabel(QStringLiteral("×"), sizeRow));
    sizeLayout->addWidget(m_heightSpin);
    sizeLayout->addStretch();
    basicForm->addRow(tr("出力サイズ:"), sizeRow);

    m_preserveAspectCheck = new QCheckBox(tr("縦横比を維持する"), basicPage);
    m_preserveAspectCheck->setChecked(true);
    basicForm->addRow(QString(), m_preserveAspectCheck);

    m_transparentCheck = new QCheckBox(tr("背景を透明にする"), basicPage);
    basicForm->addRow(QString(), m_transparentCheck);

    auto* lineOptions = new QWidget(basicPage);
    auto* lineOptionsLayout = new QHBoxLayout(lineOptions);
    lineOptionsLayout->setContentsMargins(0, 0, 0, 0);
    m_colorTraceCheck = new QCheckBox(tr("色トレス線"), lineOptions);
    m_correctionCheck = new QCheckBox(tr("作監修正"), lineOptions);
    lineOptionsLayout->addWidget(m_colorTraceCheck);
    lineOptionsLayout->addWidget(m_correctionCheck);
    lineOptionsLayout->addStretch();
    basicForm->addRow(tr("追加表示:"), lineOptions);

    tabs->addTab(basicPage, tr("基本"));

    auto* detailPage = new QWidget(tabs);
    auto* detailForm = new QFormLayout(detailPage);
    detailForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_profileLabel = new QLabel(tr("プロファイル:"), detailPage);
    m_profileCombo = new QComboBox(detailPage);
    detailForm->addRow(m_profileLabel, m_profileCombo);

    m_qualityLabel = new QLabel(tr("画質:"), detailPage);
    m_qualitySpin = new QSpinBox(detailPage);
    detailForm->addRow(m_qualityLabel, m_qualitySpin);

    m_encodeSpeedLabel = new QLabel(tr("変換速度:"), detailPage);
    m_encodeSpeedCombo = new QComboBox(detailPage);
    m_encodeSpeedCombo->addItem(tr("最速（容量大）"), QStringLiteral("ultrafast"));
    m_encodeSpeedCombo->addItem(tr("高速"), QStringLiteral("veryfast"));
    m_encodeSpeedCombo->addItem(tr("標準"), QStringLiteral("medium"));
    m_encodeSpeedCombo->addItem(tr("高圧縮（時間がかかる）"), QStringLiteral("slow"));
    setComboByData(m_encodeSpeedCombo, QStringLiteral("medium"));
    detailForm->addRow(m_encodeSpeedLabel, m_encodeSpeedCombo);

    m_fpsLabel = new QLabel(tr("出力FPS:"), detailPage);
    m_fpsSpin = new QDoubleSpinBox(detailPage);
    m_fpsSpin->setDecimals(3);
    m_fpsSpin->setRange(1.0, 120.0);
    m_fpsSpin->setValue(24.0);
    m_fpsSpin->setSuffix(tr(" fps"));
    detailForm->addRow(m_fpsLabel, m_fpsSpin);

    m_playbackSpeedCombo = new QComboBox(detailPage);
    m_playbackSpeedCombo->addItem(tr("25%（4倍ゆっくり）"), 25);
    m_playbackSpeedCombo->addItem(tr("50%（2倍ゆっくり）"), 50);
    m_playbackSpeedCombo->addItem(tr("100%（通常）"), 100);
    m_playbackSpeedCombo->addItem(tr("150%"), 150);
    m_playbackSpeedCombo->addItem(tr("200%（2倍速）"), 200);
    m_playbackSpeedCombo->addItem(tr("400%（4倍速）"), 400);
    setComboByData(m_playbackSpeedCombo, 100);
    detailForm->addRow(tr("再生速度:"), m_playbackSpeedCombo);

    m_sequencePrefixLabel = new QLabel(tr("連番名:"), detailPage);
    m_sequencePrefixEdit = new QLineEdit(QStringLiteral("frame_"), detailPage);
    detailForm->addRow(m_sequencePrefixLabel, m_sequencePrefixEdit);

    m_sequenceStartLabel = new QLabel(tr("開始番号:"), detailPage);
    m_sequenceStartSpin = new QSpinBox(detailPage);
    m_sequenceStartSpin->setRange(0, 99999999);
    m_sequenceStartSpin->setValue(1);
    detailForm->addRow(m_sequenceStartLabel, m_sequenceStartSpin);

    m_sequencePaddingLabel = new QLabel(tr("番号桁数:"), detailPage);
    m_sequencePaddingSpin = new QSpinBox(detailPage);
    m_sequencePaddingSpin->setRange(2, 8);
    m_sequencePaddingSpin->setValue(4);
    detailForm->addRow(m_sequencePaddingLabel, m_sequencePaddingSpin);

    detailForm->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    tabs->addTab(detailPage, tr("画質・時間"));

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("書き出し"));
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("キャンセル"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabs, 1);
    mainLayout->addWidget(m_buttonBox);

    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browseOutputPath);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_fromSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (value > m_toSpin->value()) m_toSpin->setValue(value);
    });
    connect(m_toSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (value < m_fromSpin->value()) m_fromSpin->setValue(value);
    });
    connect(m_presetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExportDialog::applyPreset);
    connect(m_formatCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        rebuildProfileOptions();
        updateFormatDependentUi();
        markPresetCustom();
    });
    connect(m_profileCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateFormatDependentUi();
        markPresetCustom();
    });
    connect(m_contentCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateFormatDependentUi();
        markPresetCustom();
    });
    connect(m_scopeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateFormatDependentUi();
        markPresetCustom();
    });
    connect(m_resolutionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateResolutionUi();
        markPresetCustom();
    });
    connect(m_qualitySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { markPresetCustom(); });
    connect(m_encodeSpeedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { markPresetCustom(); });
    connect(m_fpsSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { markPresetCustom(); });
    connect(m_playbackSpeedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { markPresetCustom(); });
    connect(m_widthSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { markPresetCustom(); });
    connect(m_heightSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { markPresetCustom(); });
    connect(m_preserveAspectCheck, &QCheckBox::toggled, this, [this](bool) { markPresetCustom(); });
    connect(m_transparentCheck, &QCheckBox::toggled, this, [this](bool) { markPresetCustom(); });
    connect(m_colorTraceCheck, &QCheckBox::toggled, this, [this](bool) { markPresetCustom(); });
    connect(m_correctionCheck, &QCheckBox::toggled, this, [this](bool) { markPresetCustom(); });
    connect(m_celCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { markPresetCustom(); });
    connect(m_fromSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { markPresetCustom(); });
    connect(m_toSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { markPresetCustom(); });
    connect(m_sequencePrefixEdit, &QLineEdit::textEdited, this, [this](const QString&) { markPresetCustom(); });
    connect(m_sequenceStartSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { markPresetCustom(); });
    connect(m_sequencePaddingSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { markPresetCustom(); });

    rebuildProfileOptions();
    updateResolutionUi();
    applyPreset(hasFfmpeg ? 0 : 4);
}

void ExportDialog::applyPreset(int presetIndex) {
    if (presetIndex < 0 || presetIndex >= 6) return;
    m_applyingPreset = true;
    switch (presetIndex) {
        case 0:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::Mp4H264));
            m_qualitySpin->setValue(20);
            setComboByData(m_encodeSpeedCombo, QStringLiteral("veryfast"));
            m_transparentCheck->setChecked(false);
            break;
        case 1:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::MovProRes));
            setComboByData(m_profileCombo, 3);
            m_transparentCheck->setChecked(false);
            break;
        case 2:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::MovDnxhr));
            setComboByData(m_profileCombo, QStringLiteral("dnxhr_hqx"));
            m_transparentCheck->setChecked(false);
            break;
        case 3:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::MovProRes));
            setComboByData(m_profileCombo, 4);
            setComboByData(m_contentCombo, static_cast<int>(ExportContent::Drawing));
            m_transparentCheck->setChecked(true);
            break;
        case 4:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::PngSequence));
            m_qualitySpin->setValue(6);
            break;
        case 5:
            setComboByData(m_formatCombo, static_cast<int>(ExportFormat::OpenExrSequence));
            setComboByData(m_profileCombo, 3);
            break;
        default:
            break;
    }
    rebuildProfileOptions();
    updateFormatDependentUi();
    m_presetCombo->setCurrentIndex(presetIndex);
    m_applyingPreset = false;
}

void ExportDialog::markPresetCustom() {
    if (!m_applyingPreset && m_presetCombo->currentIndex() != 6) {
        m_applyingPreset = true;
        m_presetCombo->setCurrentIndex(6);
        m_applyingPreset = false;
    }
}

void ExportDialog::rebuildProfileOptions() {
    const QVariant previous = m_profileCombo->currentData();
    m_profileCombo->clear();
    switch (format()) {
        case ExportFormat::MovProRes:
            m_profileCombo->addItem(tr("Proxy"), 0);
            m_profileCombo->addItem(tr("422 LT"), 1);
            m_profileCombo->addItem(tr("422"), 2);
            m_profileCombo->addItem(tr("422 HQ"), 3);
            m_profileCombo->addItem(tr("4444（アルファ対応）"), 4);
            m_profileCombo->addItem(tr("4444 XQ（アルファ対応）"), 5);
            if (!previous.isValid() || !setComboByData(m_profileCombo, previous)) setComboByData(m_profileCombo, 3);
            break;
        case ExportFormat::MovDnxhr:
            m_profileCombo->addItem(tr("LB（軽量プロキシ）"), QStringLiteral("dnxhr_lb"));
            m_profileCombo->addItem(tr("SQ"), QStringLiteral("dnxhr_sq"));
            m_profileCombo->addItem(tr("HQ"), QStringLiteral("dnxhr_hq"));
            m_profileCombo->addItem(tr("HQX（10bit）"), QStringLiteral("dnxhr_hqx"));
            m_profileCombo->addItem(tr("444（10bit）"), QStringLiteral("dnxhr_444"));
            if (!previous.isValid() || !setComboByData(m_profileCombo, previous)) {
                setComboByData(m_profileCombo, QStringLiteral("dnxhr_hqx"));
            }
            break;
        case ExportFormat::OpenExrSequence:
            m_profileCombo->addItem(tr("ZIP16（可逆）"), 3);
            m_profileCombo->addItem(tr("ZIP1（可逆）"), 2);
            m_profileCombo->addItem(tr("RLE（可逆）"), 1);
            m_profileCombo->addItem(tr("無圧縮"), 0);
            if (!previous.isValid() || !setComboByData(m_profileCombo, previous)) setComboByData(m_profileCombo, 3);
            break;
        case ExportFormat::DpxSequence:
            m_profileCombo->addItem(tr("10bit RGB"), 10);
            m_profileCombo->addItem(tr("16bit RGB"), 16);
            if (!previous.isValid() || !setComboByData(m_profileCombo, previous)) setComboByData(m_profileCombo, 10);
            break;
        default:
            m_profileCombo->addItem(tr("標準"));
            break;
    }
}

void ExportDialog::updateFormatDependentUi() {
    const ExportFormat selectedFormat = format();
    const bool sequence = perapera::ui::isImageSequence(selectedFormat);
    const bool hasDrawing = content() != ExportContent::Previz;
    const bool range = scope() == ExportScope::CurrentCutRange;
    const bool hasProfile = selectedFormat == ExportFormat::MovProRes || selectedFormat == ExportFormat::MovDnxhr ||
                            selectedFormat == ExportFormat::OpenExrSequence ||
                            selectedFormat == ExportFormat::DpxSequence;
    const bool h264 = selectedFormat == ExportFormat::Mp4H264;
    const bool png = selectedFormat == ExportFormat::PngSequence;
    const bool jpeg = selectedFormat == ExportFormat::JpegSequence;
    const int formatIndex = static_cast<int>(selectedFormat);
    const bool formatChanged = formatIndex != m_lastQualityFormat;

    m_fromSpin->setEnabled(range);
    m_toSpin->setEnabled(range);
    m_celCombo->setEnabled(hasDrawing);
    m_colorTraceCheck->setEnabled(hasDrawing);
    m_correctionCheck->setEnabled(hasDrawing);

    m_profileLabel->setVisible(hasProfile);
    m_profileCombo->setVisible(hasProfile);
    m_qualityLabel->setVisible(h264 || png || jpeg);
    m_qualitySpin->setVisible(h264 || png || jpeg);
    m_encodeSpeedLabel->setVisible(h264);
    m_encodeSpeedCombo->setVisible(h264);
    m_fpsLabel->setVisible(!sequence);
    m_fpsSpin->setVisible(!sequence);

    if (h264) {
        m_qualityLabel->setText(tr("画質（CRF・小さいほど高画質）:"));
        m_qualitySpin->setRange(0, 51);
        if (formatChanged) m_qualitySpin->setValue(18);
    } else if (jpeg) {
        m_qualityLabel->setText(tr("JPEG画質:"));
        m_qualitySpin->setRange(1, 100);
        if (formatChanged) m_qualitySpin->setValue(95);
    } else if (png) {
        m_qualityLabel->setText(tr("PNG圧縮（画質は可逆）:"));
        m_qualitySpin->setRange(0, 9);
        if (formatChanged) m_qualitySpin->setValue(6);
    } else {
        m_qualityLabel->setText(tr("画質:"));
    }
    m_lastQualityFormat = formatIndex;

    const bool alphaAllowed = hasDrawing && content() == ExportContent::Drawing &&
                              perapera::ui::supportsAlpha(selectedFormat, currentProResProfile());
    m_transparentCheck->setEnabled(alphaAllowed);
    if (!alphaAllowed) m_transparentCheck->setChecked(false);

    m_sequencePrefixLabel->setVisible(sequence);
    m_sequencePrefixEdit->setVisible(sequence);
    m_sequenceStartLabel->setVisible(sequence);
    m_sequenceStartSpin->setVisible(sequence);
    m_sequencePaddingLabel->setVisible(sequence);
    m_sequencePaddingSpin->setVisible(sequence);
}

void ExportDialog::updateResolutionUi() {
    const ResolutionPreset preset = currentEnum<ResolutionPreset>(m_resolutionCombo);
    int width = m_canvasWidth;
    int height = m_canvasHeight;
    switch (preset) {
        case ResolutionPreset::Half:
            width = std::max(16, m_canvasWidth / 2);
            height = std::max(16, m_canvasHeight / 2);
            break;
        case ResolutionPreset::Double:
            width = std::min(16384, m_canvasWidth * 2);
            height = std::min(16384, m_canvasHeight * 2);
            break;
        case ResolutionPreset::Hd:
            width = 1280;
            height = 720;
            break;
        case ResolutionPreset::FullHd:
            width = 1920;
            height = 1080;
            break;
        case ResolutionPreset::Uhd:
            width = 3840;
            height = 2160;
            break;
        case ResolutionPreset::Custom:
            m_widthSpin->setEnabled(true);
            m_heightSpin->setEnabled(true);
            return;
        case ResolutionPreset::Canvas:
            break;
    }
    m_widthSpin->setValue(width);
    m_heightSpin->setValue(height);
    m_widthSpin->setEnabled(false);
    m_heightSpin->setEnabled(false);
}

void ExportDialog::browseOutputPath() {
    if (perapera::ui::isImageSequence(format())) {
        const QString dir =
            QFileDialog::getExistingDirectory(this, tr("出力先フォルダを選択"), m_outputPathEdit->text());
        if (!dir.isEmpty()) m_outputPathEdit->setText(dir);
        return;
    }

    const QString extension = perapera::ui::exportExtension(format());
    const QString filter = extension == QStringLiteral("mp4") ? tr("MP4動画 (*.mp4)") : tr("MOV動画 (*.mov)");
    const QString path =
        QFileDialog::getSaveFileName(this, tr("出力先ファイルを選択"), m_outputPathEdit->text(), filter);
    if (!path.isEmpty()) m_outputPathEdit->setText(path);
}

void ExportDialog::setOutputPath(const QString& path) {
    if (m_outputPathEdit) m_outputPathEdit->setText(path);
}

ExportSettings ExportDialog::settings() const {
    ExportSettings result;
    result.format = format();
    result.scope = scope();
    result.content = content();
    result.outputPath = m_outputPathEdit->text().trimmed();
    if (!perapera::ui::isImageSequence(result.format) && !result.outputPath.isEmpty()) {
        const QString extension = perapera::ui::exportExtension(result.format);
        if (QFileInfo(result.outputPath).suffix().compare(extension, Qt::CaseInsensitive) != 0) {
            result.outputPath += QStringLiteral(".") + extension;
        }
    }
    result.fromFrame = m_fromSpin->value();
    result.toFrame = m_toSpin->value();
    result.onlyCel = m_celCombo->currentIndex() - 1;
    result.includeColorTrace = m_colorTraceCheck->isChecked();
    result.includeCorrection = m_correctionCheck->isChecked();
    result.transparentBackground = m_transparentCheck->isEnabled() && m_transparentCheck->isChecked();
    result.outputWidth = m_widthSpin->value();
    result.outputHeight = m_heightSpin->value();
    result.preserveAspectRatio = m_preserveAspectCheck->isChecked();
    result.fps = m_fpsSpin->value();
    result.playbackSpeedPercent = m_playbackSpeedCombo->currentData().toInt();
    result.sequencePrefix = perapera::ui::sanitizedSequencePrefix(m_sequencePrefixEdit->text());
    result.sequenceStartNumber = m_sequenceStartSpin->value();
    result.sequencePadding = m_sequencePaddingSpin->value();
    result.h264Preset = m_encodeSpeedCombo->currentData().toString();

    switch (result.format) {
        case ExportFormat::PngSequence:
            result.pngCompression = m_qualitySpin->value();
            break;
        case ExportFormat::JpegSequence:
            result.jpegQuality = m_qualitySpin->value();
            break;
        case ExportFormat::OpenExrSequence:
            result.exrCompression = m_profileCombo->currentData().toInt();
            break;
        case ExportFormat::DpxSequence:
            result.dpxBitDepth = m_profileCombo->currentData().toInt();
            break;
        case ExportFormat::Mp4H264:
            result.h264Crf = m_qualitySpin->value();
            break;
        case ExportFormat::MovProRes:
            result.proResProfile = m_profileCombo->currentData().toInt();
            break;
        case ExportFormat::MovDnxhr:
            result.dnxhrProfile = m_profileCombo->currentData().toString();
            break;
        default:
            break;
    }
    return result;
}

void ExportDialog::accept() {
    const ExportSettings result = settings();
    if (result.outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("書き出し"), tr("出力先を指定してください。"));
        return;
    }
    if (perapera::ui::requiresFfmpeg(result.format) && perapera::ui::findFfmpegExecutable().isEmpty()) {
        QMessageBox::warning(this, tr("書き出し"),
                             tr("この形式にはffmpeg.exeが必要です。アプリと同じフォルダかPATHに置いてください。"));
        return;
    }
    QDialog::accept();
}

ExportFormat ExportDialog::format() const {
    return currentEnum<ExportFormat>(m_formatCombo);
}

ExportScope ExportDialog::scope() const {
    return currentEnum<ExportScope>(m_scopeCombo);
}

ExportContent ExportDialog::content() const {
    return currentEnum<ExportContent>(m_contentCombo);
}

int ExportDialog::currentProResProfile() const {
    return format() == ExportFormat::MovProRes ? m_profileCombo->currentData().toInt() : -1;
}
