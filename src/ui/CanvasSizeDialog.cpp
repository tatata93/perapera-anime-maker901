#include "CanvasSizeDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>
#include <vector>

namespace {
constexpr int kMinCanvasSize = 16;
constexpr int kMaxCanvasSize = 8192;

struct Preset {
    QString name;
    int width;
    int height;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> kPresets = {
        {QObject::tr("フルHD (1920×1080)"), 1920, 1080},
        {QObject::tr("4K UHD (3840×2160)"), 3840, 2160},
        {QObject::tr("DCI 4K (4096×2160)"), 4096, 2160},
        {QObject::tr("HD (1280×720)"), 1280, 720},
        {QObject::tr("4:3 HD (1440×1080)"), 1440, 1080},
        {QObject::tr("4:3 (1024×768)"), 1024, 768},
        {QObject::tr("シネスコ 2.39:1 (2048×858)"), 2048, 858},
        {QObject::tr("ビスタ 1.85:1 (1998×1080)"), 1998, 1080},
        {QObject::tr("カスタム"), 0, 0},
    };
    return kPresets;
}
}  // namespace

CanvasSizeDialog::CanvasSizeDialog(int currentW, int currentH, int currentFps, QWidget* parent)
    : QDialog(parent),
      m_originalWidth(currentW),
      m_originalHeight(currentH) {
    setWindowTitle(tr("プロジェクト設定"));
    setModal(true);
    setSizeGripEnabled(false);

    auto* canvasGroup = new QGroupBox(tr("映像"), this);
    auto* form = new QFormLayout(canvasGroup);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_presetCombo = new QComboBox(canvasGroup);
    for (const Preset& preset : presets()) m_presetCombo->addItem(preset.name);
    form->addRow(tr("規格:"), m_presetCombo);

    m_widthSpin = new QSpinBox(canvasGroup);
    m_widthSpin->setRange(kMinCanvasSize, kMaxCanvasSize);
    m_widthSpin->setSuffix(tr(" px"));
    m_widthSpin->setValue(currentW);

    m_heightSpin = new QSpinBox(canvasGroup);
    m_heightSpin->setRange(kMinCanvasSize, kMaxCanvasSize);
    m_heightSpin->setSuffix(tr(" px"));
    m_heightSpin->setValue(currentH);

    auto* dimensions = new QWidget(canvasGroup);
    auto* dimensionsLayout = new QHBoxLayout(dimensions);
    dimensionsLayout->setContentsMargins(0, 0, 0, 0);
    dimensionsLayout->addWidget(m_widthSpin, 1);
    dimensionsLayout->addWidget(new QLabel(QStringLiteral("×"), dimensions));
    dimensionsLayout->addWidget(m_heightSpin, 1);
    auto* swapButton = new QPushButton(tr("入れ替え"), dimensions);
    swapButton->setToolTip(tr("幅と高さを入れ替える"));
    dimensionsLayout->addWidget(swapButton);
    form->addRow(tr("キャンバス:"), dimensions);

    m_aspectLabel = new QLabel(canvasGroup);
    form->addRow(tr("縦横比:"), m_aspectLabel);

    m_fpsSpin = new QSpinBox(canvasGroup);
    m_fpsSpin->setRange(1, 120);
    m_fpsSpin->setSuffix(tr(" fps"));
    m_fpsSpin->setValue(std::clamp(currentFps, 1, 120));
    form->addRow(tr("フレームレート:"), m_fpsSpin);

    m_changeLabel = new QLabel(canvasGroup);
    form->addRow(tr("変更:"), m_changeLabel);

    m_resizeArtworkCheck =
        new QCheckBox(tr("既存の作画用紙も新しいキャンバスに合わせる"), this);
    m_resizeArtworkCheck->setChecked(true);
    m_resizeArtworkCheck->setToolTip(
        tr("通常セルを中央基準で調整します。独自サイズの引きセルは変更しません"));

    auto* behaviorLabel =
        new QLabel(tr("作画は拡大縮小せず中央を維持します。小さくした部分は切り抜かれ、"
                      "大きくした部分には透明な余白が加わります。"),
                   this);
    behaviorLabel->setWordWrap(true);

    m_cropWarningLabel = new QLabel(this);
    m_cropWarningLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("適用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("キャンセル"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(canvasGroup);
    layout->addWidget(m_resizeArtworkCheck);
    layout->addWidget(behaviorLabel);
    layout->addWidget(m_cropWarningLabel);
    layout->addSpacing(6);
    layout->addWidget(buttons);

    int matchIndex = static_cast<int>(presets().size()) - 1;
    for (size_t i = 0; i + 1 < presets().size(); ++i) {
        if (presets()[i].width == currentW && presets()[i].height == currentH) {
            matchIndex = static_cast<int>(i);
            break;
        }
    }
    m_presetCombo->setCurrentIndex(matchIndex);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanvasSizeDialog::applyPreset);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CanvasSizeDialog::onSizeEdited);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CanvasSizeDialog::onSizeEdited);
    connect(swapButton, &QPushButton::clicked, this, &CanvasSizeDialog::swapDimensions);
    connect(m_resizeArtworkCheck, &QCheckBox::toggled, this, &CanvasSizeDialog::updateSummary);

    updateSummary();
    setMinimumWidth(520);
    adjustSize();
}

CanvasSizeDialog::CanvasSizeDialog(int currentW, int currentH, QWidget* parent)
    : CanvasSizeDialog(currentW, currentH, 24, parent) {
    setWindowTitle(tr("キャンバスサイズ"));
    if (auto* form = qobject_cast<QFormLayout*>(m_fpsSpin->parentWidget()->layout())) {
        if (QWidget* label = form->labelForField(m_fpsSpin)) label->hide();
    }
    m_fpsSpin->hide();
    m_resizeArtworkCheck->hide();
    adjustSize();
}

void CanvasSizeDialog::applyPreset(int index) {
    const auto& list = presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Preset& preset = list[static_cast<size_t>(index)];
    if (preset.width <= 0 || preset.height <= 0) return;

    m_updatingFromPreset = true;
    m_widthSpin->setValue(preset.width);
    m_heightSpin->setValue(preset.height);
    m_updatingFromPreset = false;
    updateSummary();
}

void CanvasSizeDialog::onSizeEdited() {
    if (!m_updatingFromPreset) {
        const int customIndex = static_cast<int>(presets().size()) - 1;
        if (m_presetCombo->currentIndex() != customIndex) {
            m_updatingFromPreset = true;
            m_presetCombo->setCurrentIndex(customIndex);
            m_updatingFromPreset = false;
        }
    }
    updateSummary();
}

void CanvasSizeDialog::swapDimensions() {
    const int oldWidth = m_widthSpin->value();
    m_updatingFromPreset = true;
    m_widthSpin->setValue(m_heightSpin->value());
    m_heightSpin->setValue(oldWidth);
    m_updatingFromPreset = false;
    onSizeEdited();
}

void CanvasSizeDialog::updateSummary() {
    const int width = m_widthSpin->value();
    const int height = m_heightSpin->value();
    const int divisor = std::gcd(width, height);
    m_aspectLabel->setText(divisor > 0
                               ? QStringLiteral("%1 : %2").arg(width / divisor).arg(height / divisor)
                               : QString());
    m_changeLabel->setText(
        tr("%1×%2  →  %3×%4").arg(m_originalWidth).arg(m_originalHeight).arg(width).arg(height));

    const bool clips = width < m_originalWidth || height < m_originalHeight;
    m_cropWarningLabel->setVisible(clips && m_resizeArtworkCheck->isChecked());
    m_cropWarningLabel->setText(
        tr("注意: キャンバスが小さくなる方向では、外側の作画が切り抜かれます。"));
}

int CanvasSizeDialog::canvasWidth() const { return m_widthSpin->value(); }

int CanvasSizeDialog::canvasHeight() const { return m_heightSpin->value(); }

int CanvasSizeDialog::fps() const { return m_fpsSpin->value(); }

bool CanvasSizeDialog::resizeExistingArtwork() const {
    return m_resizeArtworkCheck->isChecked();
}
