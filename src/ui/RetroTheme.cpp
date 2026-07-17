#include "RetroTheme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace perapera::ui {
namespace {

void applyWindowsBaseStyle(QApplication& app) {
    if (auto* style = QStyleFactory::create(QStringLiteral("Windows"))) {
        app.setStyle(style);
        return;
    }
    if (auto* style = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(style);
    }
}

QPalette windows95Palette() {
    QPalette palette;
    const QColor face(212, 208, 200);
    const QColor shadow(128, 128, 128);
    const QColor dark(64, 64, 64);
    const QColor light(255, 255, 255);
    const QColor highlight(0, 0, 128);

    palette.setColor(QPalette::Window, face);
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(244, 244, 244));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::Button, face);
    palette.setColor(QPalette::ButtonText, Qt::black);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Light, light);
    palette.setColor(QPalette::Midlight, QColor(232, 232, 232));
    palette.setColor(QPalette::Mid, shadow);
    palette.setColor(QPalette::Dark, dark);
    palette.setColor(QPalette::Shadow, Qt::black);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, shadow);
    palette.setColor(QPalette::Disabled, QPalette::Text, shadow);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, shadow);
    return palette;
}

QPalette windowsXpPalette() {
    QPalette palette = windows95Palette();
    const QColor face(236, 233, 216);
    palette.setColor(QPalette::Window, face);
    palette.setColor(QPalette::Button, face);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(250, 249, 244));
    palette.setColor(QPalette::Highlight, QColor(49, 106, 197));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Mid, QColor(172, 168, 153));
    palette.setColor(QPalette::Dark, QColor(113, 111, 100));
    return palette;
}

QString windows95StyleSheet() {
    return QStringLiteral(R"(
QWidget {
    background-color: #d4d0c8;
    color: #000000;
    selection-background-color: #000080;
    selection-color: #ffffff;
}
QToolTip {
    background-color: #ffffe1;
    color: #000000;
    border: 1px solid #000000;
}
QMenuBar, QToolBar, QStatusBar {
    background-color: #d4d0c8;
    border: 1px solid #808080;
}
QMenuBar::item {
    padding: 3px 7px;
    background: transparent;
}
QMenuBar::item:selected, QMenu::item:selected {
    background-color: #000080;
    color: #ffffff;
}
QMenu {
    background-color: #d4d0c8;
    border-top: 2px solid #ffffff;
    border-left: 2px solid #ffffff;
    border-right: 2px solid #404040;
    border-bottom: 2px solid #404040;
    padding: 2px;
}
QMenu::item {
    padding: 3px 28px 3px 20px;
}
QPushButton, QToolButton {
    background-color: #d4d0c8;
    color: #000000;
    border-top: 2px solid #ffffff;
    border-left: 2px solid #ffffff;
    border-right: 2px solid #404040;
    border-bottom: 2px solid #404040;
    padding: 2px 8px;
    min-height: 19px;
}
QPushButton:pressed, QPushButton:checked, QToolButton:pressed, QToolButton:checked {
    border-top: 2px solid #404040;
    border-left: 2px solid #404040;
    border-right: 2px solid #ffffff;
    border-bottom: 2px solid #ffffff;
    padding-left: 9px;
    padding-top: 3px;
}
QPushButton:disabled, QToolButton:disabled {
    color: #808080;
}
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QListView, QListWidget,
QTreeView, QTableView, QAbstractScrollArea {
    background-color: #ffffff;
    color: #000000;
    border-top: 2px solid #404040;
    border-left: 2px solid #404040;
    border-right: 2px solid #ffffff;
    border-bottom: 2px solid #ffffff;
}
QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button,
QDoubleSpinBox::down-button {
    background-color: #d4d0c8;
    border-top: 1px solid #ffffff;
    border-left: 1px solid #ffffff;
    border-right: 1px solid #404040;
    border-bottom: 1px solid #404040;
}
QHeaderView::section {
    background-color: #d4d0c8;
    color: #000000;
    border-top: 1px solid #ffffff;
    border-left: 1px solid #ffffff;
    border-right: 1px solid #808080;
    border-bottom: 1px solid #808080;
    padding: 3px;
}
QTabWidget::pane {
    border-top: 2px solid #ffffff;
    border-left: 2px solid #ffffff;
    border-right: 2px solid #404040;
    border-bottom: 2px solid #404040;
}
QTabBar::tab {
    background-color: #d4d0c8;
    border-top: 2px solid #ffffff;
    border-left: 2px solid #ffffff;
    border-right: 2px solid #404040;
    border-bottom: 0;
    padding: 3px 10px;
}
QTabBar::tab:selected {
    margin-bottom: -1px;
}
QDockWidget::title, QGroupBox {
    background-color: #d4d0c8;
}
QGroupBox {
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-right: 2px solid #ffffff;
    border-bottom: 2px solid #ffffff;
    margin-top: 8px;
    padding-top: 8px;
}
QSlider::groove:horizontal {
    background: #ffffff;
    border-top: 2px solid #404040;
    border-left: 2px solid #404040;
    border-right: 2px solid #ffffff;
    border-bottom: 2px solid #ffffff;
    height: 4px;
}
QSlider::handle:horizontal {
    background-color: #d4d0c8;
    border-top: 2px solid #ffffff;
    border-left: 2px solid #ffffff;
    border-right: 2px solid #404040;
    border-bottom: 2px solid #404040;
    width: 12px;
    margin: -5px 0;
}
QScrollBar:horizontal, QScrollBar:vertical {
    background: #d4d0c8;
    border: 1px solid #808080;
}
QScrollBar::handle:horizontal, QScrollBar::handle:vertical {
    background: #d4d0c8;
    border-top: 1px solid #ffffff;
    border-left: 1px solid #ffffff;
    border-right: 1px solid #404040;
    border-bottom: 1px solid #404040;
    min-width: 16px;
    min-height: 16px;
}
QSplitter::handle {
    background-color: #d4d0c8;
}
)");
}

QString windowsXpStyleSheet() {
    return QStringLiteral(R"(
QWidget {
    background-color: #ece9d8;
    color: #000000;
    selection-background-color: #316ac5;
    selection-color: #ffffff;
}
QToolTip {
    background-color: #ffffe1;
    color: #000000;
    border: 1px solid #000000;
}
QMenuBar, QToolBar, QStatusBar {
    background-color: #ece9d8;
    border: 1px solid #aca899;
}
QMenuBar::item {
    padding: 3px 8px;
    background: transparent;
}
QMenuBar::item:selected, QMenu::item:selected {
    background-color: #316ac5;
    color: #ffffff;
}
QMenu {
    background-color: #ffffff;
    color: #000000;
    border: 1px solid #aca899;
    padding: 2px;
}
QMenu::item {
    padding: 4px 30px 4px 22px;
}
QPushButton, QToolButton {
    color: #000000;
    border: 1px solid #7f9db9;
    border-radius: 3px;
    padding: 3px 9px;
    min-height: 20px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffffff, stop:0.45 #f4f2e7, stop:1 #d6d2bd);
}
QPushButton:hover, QToolButton:hover {
    border: 1px solid #f2a300;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #fffdf5, stop:0.45 #fff4ce, stop:1 #ffd36a);
}
QPushButton:pressed, QPushButton:checked, QToolButton:pressed, QToolButton:checked {
    border: 1px solid #316ac5;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #c5d6f0, stop:1 #e8f0ff);
}
QPushButton:disabled, QToolButton:disabled {
    color: #8f8f8f;
    border-color: #aca899;
}
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QListView, QListWidget,
QTreeView, QTableView, QAbstractScrollArea {
    background-color: #ffffff;
    color: #000000;
    border: 1px solid #7f9db9;
}
QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button,
QDoubleSpinBox::down-button {
    background-color: #ece9d8;
    border-left: 1px solid #7f9db9;
}
QHeaderView::section {
    background-color: #ece9d8;
    color: #000000;
    border: 1px solid #aca899;
    padding: 3px;
}
QTabWidget::pane {
    border: 1px solid #919b9c;
    background: #ece9d8;
}
QTabBar::tab {
    background: #ece9d8;
    border: 1px solid #919b9c;
    border-bottom: 0;
    padding: 4px 11px;
    margin-right: 1px;
}
QTabBar::tab:selected {
    background: #ffffff;
    margin-bottom: -1px;
}
QDockWidget::title {
    color: #ffffff;
    padding: 3px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #0a246a, stop:1 #a6caf0);
}
QGroupBox {
    border: 1px solid #aca899;
    margin-top: 8px;
    padding-top: 8px;
}
QSlider::groove:horizontal {
    background: #ffffff;
    border: 1px solid #7f9db9;
    height: 4px;
}
QSlider::handle:horizontal {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #ffffff, stop:1 #d6d2bd);
    border: 1px solid #7f9db9;
    width: 13px;
    margin: -5px 0;
}
QScrollBar:horizontal, QScrollBar:vertical {
    background: #ece9d8;
    border: 1px solid #aca899;
}
QScrollBar::handle:horizontal, QScrollBar::handle:vertical {
    background: #d6d2bd;
    border: 1px solid #7f9db9;
    min-width: 16px;
    min-height: 16px;
}
QSplitter::handle {
    background-color: #aca899;
}
)");
}

}  // namespace

void applyRetroTheme(QApplication& app, RetroThemeVariant variant) {
    applyWindowsBaseStyle(app);
    if (variant == RetroThemeVariant::Windows95) {
        app.setFont(QFont(QStringLiteral("MS UI Gothic"), 9));
        app.setPalette(windows95Palette());
        app.setStyleSheet(windows95StyleSheet());
        return;
    }

    app.setFont(QFont(QStringLiteral("Tahoma"), 9));
    app.setPalette(windowsXpPalette());
    app.setStyleSheet(windowsXpStyleSheet());
}

}  // namespace perapera::ui
