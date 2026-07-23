#include "ShortcutSettings.h"

#include <QAction>
#include <QObject>
#include <QSettings>

namespace perapera::ui {
namespace {

QString scopeKey(ShortcutScope scope) {
    switch (scope) {
        case ShortcutScope::MainCanvas:
            return QStringLiteral("main");
        case ShortcutScope::Xsheet:
            return QStringLiteral("xsheet");
        case ShortcutScope::Storyboard:
            return QStringLiteral("storyboard");
        case ShortcutScope::SettingBoard:
            return QStringLiteral("settingBoard");
    }
    return QStringLiteral("main");
}

QSettings shortcutSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("perapera"),
                     QStringLiteral("perapera-anime-maker901"));
}

const ShortcutDefinition* findDefinition(ShortcutScope scope, const QString& id) {
    const QList<ShortcutDefinition>& definitions = shortcutDefinitions(scope);
    for (const ShortcutDefinition& definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

}  // namespace

QString shortcutScopeLabel(ShortcutScope scope) {
    switch (scope) {
        case ShortcutScope::MainCanvas:
            return QObject::tr("作画キャンバス");
        case ShortcutScope::Xsheet:
            return QObject::tr("タイムシート");
        case ShortcutScope::Storyboard:
            return QObject::tr("絵コンテ");
        case ShortcutScope::SettingBoard:
            return QObject::tr("設定ボード");
    }
    return {};
}

const QList<ShortcutDefinition>& shortcutDefinitions(ShortcutScope scope) {
    static const QList<ShortcutDefinition> mainDefinitions{
        {QStringLiteral("undo"), QObject::tr("元に戻す"), QKeySequence(QStringLiteral("Ctrl+Z"))},
        {QStringLiteral("redo"), QObject::tr("やり直す"), QKeySequence(QStringLiteral("Ctrl+Y"))},
        {QStringLiteral("pen"), QObject::tr("ペン"), QKeySequence(QStringLiteral("B"))},
        {QStringLiteral("eraser"), QObject::tr("消しゴム"), QKeySequence(QStringLiteral("E"))},
        {QStringLiteral("fill"), QObject::tr("塗りつぶし"), QKeySequence(QStringLiteral("G"))},
        {QStringLiteral("lassoFill"), QObject::tr("投げ縄塗り"), QKeySequence(QStringLiteral("L"))},
        {QStringLiteral("move"), QObject::tr("移動"), QKeySequence(QStringLiteral("V"))},
        {QStringLiteral("eyedropper"), QObject::tr("スポイト"), QKeySequence(QStringLiteral("I"))},
        {QStringLiteral("previousFrame"), QObject::tr("前のコマ"), QKeySequence(QStringLiteral("A"))},
        {QStringLiteral("nextFrame"), QObject::tr("次のコマ"), QKeySequence(QStringLiteral("D"))},
        {QStringLiteral("previousCel"), QObject::tr("左（奥）のセル"), QKeySequence(QStringLiteral("W"))},
        {QStringLiteral("nextCel"), QObject::tr("右（手前）のセル"), QKeySequence(QStringLiteral("S"))},
        {QStringLiteral("play"), QObject::tr("再生・停止"), QKeySequence(QStringLiteral("Space"))},
        {QStringLiteral("step1"), QObject::tr("1コマ打ち"), QKeySequence(QStringLiteral("1"))},
        {QStringLiteral("step2"), QObject::tr("2コマ打ち"), QKeySequence(QStringLiteral("2"))},
        {QStringLiteral("step3"), QObject::tr("3コマ打ち"), QKeySequence(QStringLiteral("3"))},
        {QStringLiteral("onion"), QObject::tr("オニオンスキン"), QKeySequence(QStringLiteral("O"))},
    };
    static const QList<ShortcutDefinition> storyboardDefinitions{
        {QStringLiteral("undo"), QObject::tr("元に戻す"), QKeySequence(QStringLiteral("Ctrl+Z"))},
        {QStringLiteral("redo"), QObject::tr("やり直す"), QKeySequence(QStringLiteral("Ctrl+Y"))},
        {QStringLiteral("pen"), QObject::tr("ペン"), QKeySequence(QStringLiteral("B"))},
        {QStringLiteral("eraser"), QObject::tr("消しゴム"), QKeySequence(QStringLiteral("E"))},
        {QStringLiteral("fill"), QObject::tr("塗りつぶし"), QKeySequence(QStringLiteral("G"))},
        {QStringLiteral("lassoFill"), QObject::tr("投げ縄塗り"), QKeySequence(QStringLiteral("L"))},
        {QStringLiteral("eyedropper"), QObject::tr("スポイト"), QKeySequence(QStringLiteral("I"))},
    };
    static const QList<ShortcutDefinition> xsheetDefinitions{
        {QStringLiteral("copy"), QObject::tr("割付をコピー"), QKeySequence(QStringLiteral("Ctrl+C"))},
        {QStringLiteral("cut"), QObject::tr("割付を切り取り"), QKeySequence(QStringLiteral("Ctrl+X"))},
        {QStringLiteral("paste"), QObject::tr("割付を貼り付け"), QKeySequence(QStringLiteral("Ctrl+V"))},
        {QStringLiteral("clear"), QObject::tr("割付を空セルにする"), QKeySequence(QStringLiteral("Delete"))},
        {QStringLiteral("hold"), QObject::tr("同じ絵を延長"), QKeySequence(QStringLiteral("H"))},
    };
    static const QList<ShortcutDefinition> settingBoardDefinitions{
        {QStringLiteral("undo"), QObject::tr("元に戻す"), QKeySequence(QStringLiteral("Ctrl+Z"))},
        {QStringLiteral("redo"), QObject::tr("やり直す"), QKeySequence(QStringLiteral("Ctrl+Y"))},
        {QStringLiteral("pen"), QObject::tr("ペン"), QKeySequence(QStringLiteral("B"))},
        {QStringLiteral("eraser"), QObject::tr("消しゴム"), QKeySequence(QStringLiteral("E"))},
        {QStringLiteral("fill"), QObject::tr("塗りつぶし"), QKeySequence(QStringLiteral("G"))},
        {QStringLiteral("lassoFill"), QObject::tr("投げ縄塗り"), QKeySequence(QStringLiteral("L"))},
        {QStringLiteral("eyedropper"), QObject::tr("スポイト"), QKeySequence(QStringLiteral("I"))},
        {QStringLiteral("text"), QObject::tr("文字ボックス"), QKeySequence(QStringLiteral("T"))},
    };

    switch (scope) {
        case ShortcutScope::MainCanvas:
            return mainDefinitions;
        case ShortcutScope::Xsheet:
            return xsheetDefinitions;
        case ShortcutScope::Storyboard:
            return storyboardDefinitions;
        case ShortcutScope::SettingBoard:
            return settingBoardDefinitions;
    }
    return mainDefinitions;
}

QKeySequence shortcutSequence(ShortcutScope scope, const QString& id) {
    const ShortcutDefinition* definition = findDefinition(scope, id);
    if (!definition) return {};
    QSettings settings = shortcutSettings();
    const QString value =
        settings.value(QStringLiteral("shortcuts/%1/%2").arg(scopeKey(scope), id),
                       definition->defaultSequence.toString(QKeySequence::PortableText))
            .toString();
    return QKeySequence(value, QKeySequence::PortableText);
}

void saveShortcutSequence(ShortcutScope scope, const QString& id, const QKeySequence& sequence) {
    QSettings settings = shortcutSettings();
    settings.setValue(QStringLiteral("shortcuts/%1/%2").arg(scopeKey(scope), id),
                      sequence.toString(QKeySequence::PortableText));
}

void bindShortcut(QAction* action, ShortcutScope scope, const QString& id) {
    if (!action) return;
    action->setProperty("peraperaShortcutScope", static_cast<int>(scope));
    action->setProperty("peraperaShortcutId", id);
    action->setShortcut(shortcutSequence(scope, id));
    action->setShortcutContext(Qt::WindowShortcut);
}

void reloadShortcutActions(QObject* root, ShortcutScope scope) {
    if (!root) return;
    const QList<QAction*> actions = root->findChildren<QAction*>();
    for (QAction* action : actions) {
        if (action->property("peraperaShortcutScope").toInt() != static_cast<int>(scope)) continue;
        const QString id = action->property("peraperaShortcutId").toString();
        if (!id.isEmpty()) action->setShortcut(shortcutSequence(scope, id));
    }
}

}  // namespace perapera::ui
