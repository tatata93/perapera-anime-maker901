#include "ShortcutSettingsDialog.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/ShortcutSettings.h"

ShortcutSettingsDialog::ShortcutSettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("ショートカット設定"));
    setModal(true);

    auto* tabs = new QTabWidget(this);
    const perapera::ui::ShortcutScope scopes[] = {
        perapera::ui::ShortcutScope::MainCanvas,
        perapera::ui::ShortcutScope::Xsheet,
        perapera::ui::ShortcutScope::Storyboard,
        perapera::ui::ShortcutScope::SettingBoard,
    };
    for (const perapera::ui::ShortcutScope scope : scopes) {
        auto* page = new QWidget(tabs);
        auto* form = new QFormLayout(page);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const perapera::ui::ShortcutDefinition& definition :
             perapera::ui::shortcutDefinitions(scope)) {
            auto* edit =
                new QKeySequenceEdit(perapera::ui::shortcutSequence(scope, definition.id), page);
            edit->setMaximumSequenceLength(1);
            edit->setClearButtonEnabled(true);
            edit->setProperty("shortcutScope", static_cast<int>(scope));
            edit->setProperty("shortcutId", definition.id);
            edit->setProperty(
                "defaultShortcut",
                definition.defaultSequence.toString(QKeySequence::PortableText));
            form->addRow(definition.label + tr(":"), edit);
            m_shortcutEdits.append(edit);
        }
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);

        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        tabs->addTab(scroll, perapera::ui::shortcutScopeLabel(scope));
    }

    auto* resetButton = new QPushButton(tr("初期設定に戻す"), this);
    connect(resetButton, &QPushButton::clicked,
            this, &ShortcutSettingsDialog::resetToDefaults);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("キャンセル"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* bottom = new QHBoxLayout();
    bottom->addWidget(resetButton);
    bottom->addStretch();
    bottom->addWidget(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addLayout(bottom);
    resize(620, 560);
}

void ShortcutSettingsDialog::accept() {
    if (saveSettings()) QDialog::accept();
}

bool ShortcutSettingsDialog::saveSettings() {
    QMap<QString, QKeySequenceEdit*> assigned;
    for (QKeySequenceEdit* edit : m_shortcutEdits) {
        const QString sequence =
            edit->keySequence().toString(QKeySequence::PortableText);
        if (sequence.isEmpty()) continue;
        const QString uniqueKey =
            QStringLiteral("%1/%2")
                .arg(edit->property("shortcutScope").toInt())
                .arg(sequence);
        if (assigned.contains(uniqueKey)) {
            QMessageBox::warning(
                this, tr("ショートカットが重複しています"),
                tr("同じウィンドウでは、同じキーを複数の操作に割り当てられません。"));
            edit->setFocus();
            return false;
        }
        assigned.insert(uniqueKey, edit);
    }

    for (QKeySequenceEdit* edit : m_shortcutEdits) {
        const auto scope = static_cast<perapera::ui::ShortcutScope>(
            edit->property("shortcutScope").toInt());
        perapera::ui::saveShortcutSequence(
            scope, edit->property("shortcutId").toString(), edit->keySequence());
    }
    return true;
}

void ShortcutSettingsDialog::resetToDefaults() {
    for (QKeySequenceEdit* edit : m_shortcutEdits) {
        edit->setKeySequence(QKeySequence(
            edit->property("defaultShortcut").toString(),
            QKeySequence::PortableText));
    }
}
