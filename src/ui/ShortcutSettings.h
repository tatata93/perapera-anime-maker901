#pragma once

#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;
class QObject;

namespace perapera::ui {

enum class ShortcutScope {
    MainCanvas,
    Xsheet,
    Storyboard,
    SettingBoard,
};

struct ShortcutDefinition {
    QString id;
    QString label;
    QKeySequence defaultSequence;
};

QString shortcutScopeLabel(ShortcutScope scope);
const QList<ShortcutDefinition>& shortcutDefinitions(ShortcutScope scope);
QKeySequence shortcutSequence(ShortcutScope scope, const QString& id);
void saveShortcutSequence(ShortcutScope scope, const QString& id, const QKeySequence& sequence);
void bindShortcut(QAction* action, ShortcutScope scope, const QString& id);
void reloadShortcutActions(QObject* root, ShortcutScope scope);

}  // namespace perapera::ui
