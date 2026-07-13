#include "NewProjectDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <numeric>
#include <vector>

namespace {

struct Preset {
    QString name;
    int width;
    int height;
};

// キャンバス解像度プリセット(CanvasSizeDialogと同じ並び)。末尾「カスタム」(<=0)は自動セットしない
const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {QObject::tr("フルHD (1920x1080)"), 1920, 1080},
        {QObject::tr("4K UHD (3840x2160)"), 3840, 2160},
        {QObject::tr("DCI 4K (4096x2160)"), 4096, 2160},
        {QObject::tr("HD (1280x720)"), 1280, 720},
        {QObject::tr("SD 4:3 (640x480)"), 640, 480},
        {QObject::tr("4:3 (1440x1080)"), 1440, 1080},
        {QObject::tr("シネスコ 2.39:1 (1920x804)"), 1920, 804},
        {QObject::tr("ビスタ 1.85:1 (1920x1038)"), 1920, 1038},
        {QObject::tr("カスタム"), 0, 0},
    };
    return list;
}

}  // namespace

NewProjectDialog::NewProjectDialog(int defaultFps, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("新規プロジェクト"));

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_nameEdit = new QLineEdit(tr("Untitled"), this);
    form->addRow(tr("プロジェクト名:"), m_nameEdit);

    m_presetCombo = new QComboBox(this);
    for (const Preset& p : presets()) m_presetCombo->addItem(p.name);
    form->addRow(tr("プリセット:"), m_presetCombo);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(16, 8192);
    m_widthSpin->setValue(1920);
    form->addRow(tr("幅(px):"), m_widthSpin);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(16, 8192);
    m_heightSpin->setValue(1080);
    form->addRow(tr("高さ(px):"), m_heightSpin);

    m_aspectLabel = new QLabel(this);
    form->addRow(tr("アスペクト比:"), m_aspectLabel);

    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(1, 60);
    m_fpsSpin->setValue(defaultFps > 0 ? defaultFps : 24);
    m_fpsSpin->setSuffix(tr(" fps"));
    form->addRow(tr("フレームレート:"), m_fpsSpin);

    root->addLayout(form);

    m_presetCombo->setCurrentIndex(0);  // フルHD
    updateAspectLabel();

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NewProjectDialog::applyPreset);
    connect(m_widthSpin, &QSpinBox::valueChanged, this, &NewProjectDialog::onSpinEdited);
    connect(m_heightSpin, &QSpinBox::valueChanged, this, &NewProjectDialog::onSpinEdited);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttonBox);
}

void NewProjectDialog::applyPreset(int index) {
    const auto& list = presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Preset& p = list[index];
    if (p.width <= 0 || p.height <= 0) return;  // カスタム: スピンはそのまま
    m_updatingFromPreset = true;
    m_widthSpin->setValue(p.width);
    m_heightSpin->setValue(p.height);
    m_updatingFromPreset = false;
    updateAspectLabel();
}

void NewProjectDialog::onSpinEdited() {
    if (!m_updatingFromPreset) {
        const int customIndex = static_cast<int>(presets().size()) - 1;
        if (m_presetCombo->currentIndex() != customIndex) {
            const QSignalBlocker blocker(m_presetCombo);
            m_presetCombo->setCurrentIndex(customIndex);
        }
    }
    updateAspectLabel();
}

void NewProjectDialog::updateAspectLabel() {
    const int w = m_widthSpin->value();
    const int h = m_heightSpin->value();
    if (h <= 0) {
        m_aspectLabel->setText(QStringLiteral("-"));
        return;
    }
    const int g = std::gcd(w, h);
    m_aspectLabel->setText(QStringLiteral("%1:%2  (%3)").arg(w / g).arg(h / g).arg(static_cast<double>(w) / h, 0, 'f', 3));
}

QString NewProjectDialog::projectName() const {
    const QString name = m_nameEdit->text().trimmed();
    return name.isEmpty() ? tr("Untitled") : name;
}

int NewProjectDialog::canvasWidth() const { return m_widthSpin->value(); }
int NewProjectDialog::canvasHeight() const { return m_heightSpin->value(); }
int NewProjectDialog::fps() const { return m_fpsSpin->value(); }
