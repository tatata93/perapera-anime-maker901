#pragma once

class QApplication;
class QDockWidget;
class QMainWindow;
class QWidget;

namespace perapera::ui {

enum class RetroThemeVariant {
    Windows95,
    WindowsXp,
};

void applyRetroTheme(QApplication& app, RetroThemeVariant variant);
void clearRetroTheme(QApplication& app);
void setRetroThemeAvailable(QApplication& app, bool available);
bool isRetroThemeAvailable();
bool isRetroThemeEnabled();
RetroThemeVariant activeRetroThemeVariant();

void installRetroDockTitleBars(QWidget* root);
void removeRetroDockTitleBars(QWidget* root);
void installRetroWindowFrame(QMainWindow* window);
void removeRetroWindowFrame(QMainWindow* window);
void keepWindowOnScreen(QWidget* window);

}  // namespace perapera::ui
