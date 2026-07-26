#pragma once

#include <QDialog>
#include <QList>

class QKeySequenceEdit;

class ShortcutSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutSettingsDialog(QWidget* parent = nullptr);

private:
    void accept() override;
    bool saveSettings();
    void resetToDefaults();

    QList<QKeySequenceEdit*> m_shortcutEdits;
};
