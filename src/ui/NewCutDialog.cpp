#include "NewCutDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

NewCutDialog::NewCutDialog(const QString& suggestedName, int defaultFps, QWidget* parent)
    : QDialog(parent), m_fps(defaultFps > 0 ? defaultFps : 24) {
    setWindowTitle(tr("新規カット"));

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_nameEdit = new QLineEdit(suggestedName, this);
    form->addRow(tr("カット名:"), m_nameEdit);

    m_frameSpin = new QSpinBox(this);
    m_frameSpin->setRange(1, 100000);
    m_frameSpin->setValue(m_fps);  // 既定は1秒ぶん
    m_frameSpin->setSuffix(tr(" コマ"));
    form->addRow(tr("尺:"), m_frameSpin);

    m_secondsLabel = new QLabel(this);
    form->addRow(tr("尺(秒):"), m_secondsLabel);

    m_statusCombo = new QComboBox(this);
    m_statusCombo->addItems({tr("未着手"), tr("レイアウト"), tr("原画"), tr("動画"), tr("仕上げ"), tr("撮影"), tr("完成")});
    m_statusCombo->setCurrentIndex(0);  // 未着手
    form->addRow(tr("進捗:"), m_statusCombo);

    m_celCountSpin = new QSpinBox(this);
    m_celCountSpin->setRange(1, 26);
    m_celCountSpin->setValue(1);
    m_celCountSpin->setToolTip(tr("初期セル枚数(A/B/Cセル…として作成)"));
    form->addRow(tr("初期セル枚数:"), m_celCountSpin);

    m_actionEdit = new QPlainTextEdit(this);
    m_actionEdit->setPlaceholderText(tr("絵コンテの内容(アクション)"));
    m_actionEdit->setMaximumHeight(60);
    form->addRow(tr("内容:"), m_actionEdit);

    m_dialogueEdit = new QPlainTextEdit(this);
    m_dialogueEdit->setPlaceholderText(tr("セリフ"));
    m_dialogueEdit->setMaximumHeight(60);
    form->addRow(tr("セリフ:"), m_dialogueEdit);

    root->addLayout(form);

    updateSecondsLabel();
    connect(m_frameSpin, &QSpinBox::valueChanged, this, &NewCutDialog::updateSecondsLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_buttonBox);
}

void NewCutDialog::updateSecondsLabel() {
    const double seconds = static_cast<double>(m_frameSpin->value()) / std::max(1, m_fps);
    m_secondsLabel->setText(QStringLiteral("%1 秒 (%2 fps)").arg(seconds, 0, 'f', 2).arg(m_fps));
}

QString NewCutDialog::cutName() const {
    const QString name = m_nameEdit->text().trimmed();
    return name.isEmpty() ? tr("カット") : name;
}
int NewCutDialog::frameCount() const { return m_frameSpin->value(); }
int NewCutDialog::status() const { return m_statusCombo->currentIndex(); }
QString NewCutDialog::action() const { return m_actionEdit->toPlainText(); }
QString NewCutDialog::dialogue() const { return m_dialogueEdit->toPlainText(); }
int NewCutDialog::celCount() const { return m_celCountSpin->value(); }
