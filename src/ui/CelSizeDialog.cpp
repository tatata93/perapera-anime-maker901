#include "CelSizeDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kMinPaperSize = 16;
constexpr int kMaxPaperSize = 16384;
}  // namespace

CelSizeDialog::CelSizeDialog(int currentW, int currentH, int canvasW, int canvasH, QWidget* parent)
    : QDialog(parent), m_canvasWidth(canvasW), m_canvasHeight(canvasH) {
    setWindowTitle(tr("セルサイズ"));

    // 0(既定=キャンバスサイズに従う)の場合は現在有効なサイズ(キャンバスサイズ)を表示する
    const int initialW = currentW > 0 ? currentW : canvasW;
    const int initialH = currentH > 0 ? currentH : canvasH;

    auto* formLayout = new QFormLayout();
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(kMinPaperSize, kMaxPaperSize);
    m_widthSpin->setValue(initialW);
    formLayout->addRow(tr("幅:"), m_widthSpin);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(kMinPaperSize, kMaxPaperSize);
    m_heightSpin->setValue(initialH);
    formLayout->addRow(tr("高さ:"), m_heightSpin);

    // プリセット: 引きセルでよく使う倍率をワンクリックで設定する
    auto* presetRow = new QWidget(this);
    auto* presetLayout = new QHBoxLayout(presetRow);
    presetLayout->setContentsMargins(0, 0, 0, 0);

    auto* standardButton = new QPushButton(tr("標準(キャンバス)"), presetRow);
    connect(standardButton, &QPushButton::clicked, this, [this] {
        m_widthSpin->setValue(m_canvasWidth);
        m_heightSpin->setValue(m_canvasHeight);
    });
    auto* wideButton = new QPushButton(tr("横2倍"), presetRow);
    connect(wideButton, &QPushButton::clicked, this, [this] {
        m_widthSpin->setValue(m_canvasWidth * 2);
        m_heightSpin->setValue(m_canvasHeight);
    });
    auto* tallButton = new QPushButton(tr("縦2倍"), presetRow);
    connect(tallButton, &QPushButton::clicked, this, [this] {
        m_widthSpin->setValue(m_canvasWidth);
        m_heightSpin->setValue(m_canvasHeight * 2);
    });
    presetLayout->addWidget(standardButton);
    presetLayout->addWidget(wideButton);
    presetLayout->addWidget(tallButton);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(new QLabel(tr("プリセット:"), this));
    mainLayout->addWidget(presetRow);
    mainLayout->addWidget(buttonBox);
}

int CelSizeDialog::paperWidth() const {
    return m_widthSpin->value();
}

int CelSizeDialog::paperHeight() const {
    return m_heightSpin->value();
}
