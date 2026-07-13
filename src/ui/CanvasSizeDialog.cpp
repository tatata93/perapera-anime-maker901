#include "CanvasSizeDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
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

// プリセット一覧。末尾の「カスタム」(width/height<=0)は選択してもスピンを書き換えない
// 特別なエントリで、現在サイズがどのプリセットにも一致しない場合の既定選択にもなる
const std::vector<Preset>& presets() {
    static const std::vector<Preset> kPresets = {
        {QObject::tr("フルHD (1920x1080)"), 1920, 1080},
        {QObject::tr("4K UHD (3840x2160)"), 3840, 2160},
        {QObject::tr("DCI 4K (4096x2160)"), 4096, 2160},
        {QObject::tr("HD (1280x720)"), 1280, 720},
        {QObject::tr("4:3 SD (1440x1080)"), 1440, 1080},
        {QObject::tr("4:3 (1024x768)"), 1024, 768},
        {QObject::tr("シネスコ 2.39:1 (2048x858)"), 2048, 858},
        {QObject::tr("ビスタ 1.85:1 (1998x1080)"), 1998, 1080},
        {QObject::tr("カスタム"), 0, 0},
    };
    return kPresets;
}
}  // namespace

CanvasSizeDialog::CanvasSizeDialog(int currentW, int currentH, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("プロジェクト設定"));

    auto* formLayout = new QFormLayout();

    m_presetCombo = new QComboBox(this);
    for (const Preset& p : presets()) m_presetCombo->addItem(p.name);
    formLayout->addRow(tr("プリセット:"), m_presetCombo);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(kMinCanvasSize, kMaxCanvasSize);
    m_widthSpin->setValue(currentW);
    formLayout->addRow(tr("幅:"), m_widthSpin);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(kMinCanvasSize, kMaxCanvasSize);
    m_heightSpin->setValue(currentH);
    formLayout->addRow(tr("高さ:"), m_heightSpin);

    m_aspectLabel = new QLabel(this);
    formLayout->addRow(tr("アスペクト比:"), m_aspectLabel);

    // 現在サイズに一致するプリセットがあれば選択、無ければ末尾の「カスタム」を選択する
    // (この時点ではまだシグナル未接続なので、スピン値は書き換わらない)
    int matchIndex = static_cast<int>(presets().size()) - 1;
    for (size_t i = 0; i + 1 < presets().size(); ++i) {
        if (presets()[i].width == currentW && presets()[i].height == currentH) {
            matchIndex = static_cast<int>(i);
            break;
        }
    }
    m_presetCombo->setCurrentIndex(matchIndex);
    updateAspectLabel();

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CanvasSizeDialog::applyPreset);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &CanvasSizeDialog::onSpinEdited);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &CanvasSizeDialog::onSpinEdited);

    auto* warningLabel =
        new QLabel(tr("注意: 既存の作画セルのサイズは変わりません。"
                       "ここでの変更は新規セル・合成・書き出しに反映されます。"),
                    this);
    warningLabel->setWordWrap(true);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(warningLabel);
    mainLayout->addWidget(buttonBox);
}

void CanvasSizeDialog::applyPreset(int index) {
    const auto& list = presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Preset& p = list[static_cast<size_t>(index)];
    if (p.width <= 0 || p.height <= 0) return;  // 「カスタム」: スピンはそのまま(手編集可)

    m_updatingFromPreset = true;
    m_widthSpin->setValue(p.width);
    m_heightSpin->setValue(p.height);
    m_updatingFromPreset = false;
    updateAspectLabel();
}

void CanvasSizeDialog::onSpinEdited() {
    updateAspectLabel();
    if (m_updatingFromPreset) return;  // プリセット側からの書き換え中は切替不要

    const int customIndex = static_cast<int>(presets().size()) - 1;
    if (m_presetCombo->currentIndex() != customIndex) {
        m_updatingFromPreset = true;  // applyPreset()を素通りさせる(スピン値を保持したいため)
        m_presetCombo->setCurrentIndex(customIndex);
        m_updatingFromPreset = false;
    }
}

void CanvasSizeDialog::updateAspectLabel() {
    const int w = m_widthSpin->value();
    const int h = m_heightSpin->value();
    const int g = std::gcd(w, h);
    if (g > 0) {
        m_aspectLabel->setText(QStringLiteral("%1 : %2").arg(w / g).arg(h / g));
    } else {
        m_aspectLabel->clear();
    }
}

int CanvasSizeDialog::canvasWidth() const {
    return m_widthSpin->value();
}

int CanvasSizeDialog::canvasHeight() const {
    return m_heightSpin->value();
}
