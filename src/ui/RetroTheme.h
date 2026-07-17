#pragma once

class QApplication;

namespace perapera::ui {

enum class RetroThemeVariant {
    Windows95,
    WindowsXp,
};

void applyRetroTheme(QApplication& app, RetroThemeVariant variant);

}  // namespace perapera::ui
