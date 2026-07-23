#include "CanvasSizeDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <numeric>
#include <vector>

#include "ui/ShortcutSettings.h"

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

CanvasSizeDialog::CanvasSizeDialog(int currentW, int currentH, QWidget* parent, bool includeShortcutSettings)
    : QDialog(parent), m_includeShortcutSettings(includeShortcutSettings) {
    setWindowTitle(tr("プロジェクト設定"));

    auto* canvasPage = new QWidget(this);
    auto* canvasPageLayout = new QVBoxLayout(canvasPage);
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
                    canvasPage);
    warningLabel->setWordWrap(true);
    canvasPageLayout->addLayout(formLayout);
    canvasPageLayout->addWidget(warningLabel);
    canvasPageLayout->addStretch();

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CanvasSizeDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* mainLayout = new QVBoxLayout(this);
    if (m_includeShortcutSettings) {
        auto* tabs = new QTabWidget(this);
        tabs->addTab(canvasPage, tr("キャンバス"));

        auto* shortcutScroll = new QScrollArea(tabs);
        shortcutScroll->setWidgetResizable(true);
        auto* shortcutPage = new QWidget(shortcutScroll);
        auto* shortcutLayout = new QVBoxLayout(shortcutPage);

        const perapera::ui::ShortcutScope scopes[] = {
            perapera::ui::ShortcutScope::MainCanvas,
            perapera::ui::ShortcutScope::Storyboard,
            perapera::ui::ShortcutScope::SettingBoard,
        };
        for (const perapera::ui::ShortcutScope scope : scopes) {
            auto* group = new QGroupBox(perapera::ui::shortcutScopeLabel(scope), shortcutPage);
            auto* form = new QFormLayout(group);
            for (const perapera::ui::ShortcutDefinition& definition :
                 perapera::ui::shortcutDefinitions(scope)) {
                auto* edit = new QKeySequenceEdit(perapera::ui::shortcutSequence(scope, definition.id), group);
                edit->setMaximumSequenceLength(1);
                edit->setClearButtonEnabled(true);
                edit->setProperty("shortcutScope", static_cast<int>(scope));
                edit->setProperty("shortcutId", definition.id);
                edit->setProperty("defaultShortcut",
                                  definition.defaultSequence.toString(QKeySequence::PortableText));
                form->addRow(definition.label + tr(":"), edit);
                m_shortcutEdits.append(edit);
            }
            shortcutLayout->addWidget(group);
        }

        auto* resetButton = new QPushButton(tr("初期設定に戻す"), shortcutPage);
        connect(resetButton, &QPushButton::clicked, this, [this] {
            for (QKeySequenceEdit* edit : m_shortcutEdits) {
                edit->setKeySequence(
                    QKeySequence(edit->property("defaultShortcut").toString(), QKeySequence::PortableText));
            }
        });
        shortcutLayout->addWidget(resetButton, 0, Qt::AlignLeft);
        shortcutLayout->addStretch();
        shortcutScroll->setWidget(shortcutPage);
        tabs->addTab(shortcutScroll, tr("ショートカット"));
        mainLayout->addWidget(tabs);
        resize(620, 680);
    } else {
        mainLayout->addWidget(canvasPage);
    }
    mainLayout->addWidget(buttonBox);
}

void CanvasSizeDialog::accept() {
    if (m_includeShortcutSettings && !saveShortcutSettings()) return;
    QDialog::accept();
}

bool CanvasSizeDialog::saveShortcutSettings() {
    QMap<QString, QKeySequenceEdit*> assigned;
    for (QKeySequenceEdit* edit : m_shortcutEdits) {
        const QString sequence = edit->keySequence().toString(QKeySequence::PortableText);
        if (sequence.isEmpty()) continue;
        const QString uniqueKey = QStringLiteral("%1/%2").arg(edit->property("shortcutScope").toInt()).arg(sequence);
        if (assigned.contains(uniqueKey)) {
            QMessageBox::warning(this, tr("ショートカットが重複しています"),
                                 tr("同じウインドウ内では、同じキーを複数の操作に割り当てられません。"));
            edit->setFocus();
            return false;
        }
        assigned.insert(uniqueKey, edit);
    }

    for (QKeySequenceEdit* edit : m_shortcutEdits) {
        const auto scope = static_cast<perapera::ui::ShortcutScope>(edit->property("shortcutScope").toInt());
        perapera::ui::saveShortcutSequence(scope, edit->property("shortcutId").toString(), edit->keySequence());
    }
    return true;
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
