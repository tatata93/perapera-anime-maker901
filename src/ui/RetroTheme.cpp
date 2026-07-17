#include "RetroTheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QChildEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace perapera::ui {
namespace {

constexpr const char* kAvailableProperty = "peraperaRetroThemeAvailable";
constexpr const char* kEnabledProperty = "peraperaRetroThemeEnabled";
constexpr const char* kVariantProperty = "peraperaRetroThemeVariant";
constexpr const char* kDockTitleInstalledProperty = "peraperaRetroDockTitleInstalled";
constexpr const char* kWindowFrameInstalledProperty = "peraperaRetroWindowFrameInstalled";
constexpr const char* kResizeFilterInstalledProperty = "peraperaRetroResizeFilterInstalled";
constexpr const char* kScreenGuardInstalledProperty = "peraperaWindowScreenGuardInstalled";

struct StandardAppearance {
    bool captured = false;
    QString styleName;
    QPalette palette;
    QFont font;
    QString styleSheet;
};

enum class CaptionCommand {
    Minimize,
    MaximizeRestore,
    FloatRestore,
    Close,
};

enum ResizeEdge {
    ResizeNone = 0,
    ResizeLeft = 1 << 0,
    ResizeRight = 1 << 1,
    ResizeTop = 1 << 2,
    ResizeBottom = 1 << 3,
};

StandardAppearance& standardAppearance() {
    static StandardAppearance appearance;
    return appearance;
}

void captureStandardAppearance(QApplication& app) {
    StandardAppearance& appearance = standardAppearance();
    if (appearance.captured) return;
    appearance.captured = true;
    if (app.style()) appearance.styleName = app.style()->objectName();
    appearance.palette = app.palette();
    appearance.font = app.font();
    appearance.styleSheet = app.styleSheet();
}

void restoreStandardAppearance(QApplication& app) {
    StandardAppearance& appearance = standardAppearance();
    if (!appearance.captured) return;

    bool styleRestored = false;
    if (!appearance.styleName.isEmpty()) {
        if (auto* style = QStyleFactory::create(appearance.styleName)) {
            app.setStyle(style);
            styleRestored = true;
        }
    }
    if (!styleRestored) {
        if (auto* style = QStyleFactory::create(QStringLiteral("Fusion"))) app.setStyle(style);
    }
    app.setFont(appearance.font);
    app.setPalette(appearance.palette);
    app.setStyleSheet(appearance.styleSheet);
}

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

bool isXp() {
    return activeRetroThemeVariant() == RetroThemeVariant::WindowsXp;
}

QColor titleBarStartColor(bool active) {
    if (!active) return isXp() ? QColor(122, 150, 190) : QColor(128, 128, 128);
    return isXp() ? QColor(0, 84, 227) : QColor(0, 0, 128);
}

QColor titleBarEndColor(bool active) {
    if (!active) return isXp() ? QColor(192, 202, 218) : QColor(192, 192, 192);
    return isXp() ? QColor(61, 149, 255) : QColor(16, 132, 208);
}

QRect availableScreenGeometryFor(const QWidget* window) {
    QScreen* screen = nullptr;
    if (window) {
        screen = QGuiApplication::screenAt(window->frameGeometry().center());
        if (!screen) screen = window->screen();
    }
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
}

QSize effectiveMaximumSize(const QWidget* window, const QRect& available) {
    if (!window) return available.size();
    return window->maximumSize().boundedTo(available.size());
}

QPoint clampedTopLeft(const QRect& rect, const QRect& available) {
    const int minX = available.left();
    const int minY = available.top();
    const int maxX = std::max(minX, available.right() - rect.width() + 1);
    const int maxY = std::max(minY, available.bottom() - rect.height() + 1);
    return {std::clamp(rect.left(), minX, maxX), std::clamp(rect.top(), minY, maxY)};
}

int resizeEdgesAt(const QWidget* window, const QPoint& globalPos) {
    if (!window || window->isMaximized()) return ResizeNone;
    constexpr int kResizeMargin = 12;
    const QRect frame = window->frameGeometry();
    if (!frame.adjusted(-kResizeMargin, -kResizeMargin, kResizeMargin, kResizeMargin).contains(globalPos))
        return ResizeNone;

    int edges = ResizeNone;
    if (std::abs(globalPos.x() - frame.left()) <= kResizeMargin) edges |= ResizeLeft;
    if (std::abs(globalPos.x() - frame.right()) <= kResizeMargin) edges |= ResizeRight;
    if (std::abs(globalPos.y() - frame.top()) <= kResizeMargin) edges |= ResizeTop;
    if (std::abs(globalPos.y() - frame.bottom()) <= kResizeMargin) edges |= ResizeBottom;
    return edges;
}

Qt::CursorShape cursorForResizeEdges(int edges) {
    const bool left = (edges & ResizeLeft) != 0;
    const bool right = (edges & ResizeRight) != 0;
    const bool top = (edges & ResizeTop) != 0;
    const bool bottom = (edges & ResizeBottom) != 0;
    if ((left && top) || (right && bottom)) return Qt::SizeFDiagCursor;
    if ((right && top) || (left && bottom)) return Qt::SizeBDiagCursor;
    if (left || right) return Qt::SizeHorCursor;
    if (top || bottom) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

QSize effectiveMinimumSize(const QWidget* window) {
    const QSize fallback(240, 160);
    if (!window) return fallback;
    return window->minimumSize().expandedTo(window->minimumSizeHint()).expandedTo(fallback);
}

QRect resizedGeometry(const QRect& start, int edges, const QPoint& delta, const QSize& minSize, const QSize& maxSize) {
    QRect next = start;
    const int maxW = std::max(minSize.width(), maxSize.width());
    const int maxH = std::max(minSize.height(), maxSize.height());

    if ((edges & ResizeLeft) != 0) {
        const int minLeft = start.right() - maxW + 1;
        const int maxLeft = start.right() - minSize.width() + 1;
        next.setLeft(std::clamp(start.left() + delta.x(), minLeft, maxLeft));
    } else if ((edges & ResizeRight) != 0) {
        next.setWidth(std::clamp(start.width() + delta.x(), minSize.width(), maxW));
    }

    if ((edges & ResizeTop) != 0) {
        const int minTop = start.bottom() - maxH + 1;
        const int maxTop = start.bottom() - minSize.height() + 1;
        next.setTop(std::clamp(start.top() + delta.y(), minTop, maxTop));
    } else if ((edges & ResizeBottom) != 0) {
        next.setHeight(std::clamp(start.height() + delta.y(), minSize.height(), maxH));
    }

    return next;
}

bool isInteractiveResizeBlocker(QObject* object, const QWidget* window) {
    for (QObject* current = object; current && current != window; current = current->parent()) {
        if (current->inherits("QAbstractButton") || current->inherits("QAbstractSlider") ||
            current->inherits("QAbstractSpinBox") || current->inherits("QComboBox") ||
            current->inherits("QLineEdit") || current->inherits("QTextEdit") ||
            current->inherits("QPlainTextEdit") || current->inherits("QAbstractItemView") ||
            current->inherits("QMenuBar") || current->inherits("QToolBar") || current->inherits("QTabBar")) {
            return true;
        }
    }
    return false;
}

class RetroResizeFilter : public QObject {
public:
    explicit RetroResizeFilter(QMainWindow* window) : QObject(window), m_window(window) {}

    void installOn(QObject* object) {
        if (!object) return;
        if (auto* widget = qobject_cast<QWidget*>(object)) widget->setMouseTracking(true);
        object->installEventFilter(this);
        const auto children = object->children();
        for (QObject* child : children) installOn(child);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!m_window) return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::ChildAdded) {
            auto* childEvent = static_cast<QChildEvent*>(event);
            if (childEvent->child()) installOn(childEvent->child());
            return QObject::eventFilter(watched, event);
        }

        if (!isRetroThemeEnabled() || !m_window->property(kWindowFrameInstalledProperty).toBool()) {
            clearResizeCursor();
            m_dragging = false;
            m_edges = ResizeNone;
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();

            if (!m_dragging && isInteractiveResizeBlocker(watched, m_window)) {
                clearResizeCursor();
                return QObject::eventFilter(watched, event);
            }

            if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() == Qt::LeftButton) {
                const int edges = resizeEdgesAt(m_window, globalPos);
                if (edges != ResizeNone) {
                    m_dragging = true;
                    m_edges = edges;
                    m_startGeometry = m_window->frameGeometry();
                    m_startGlobalPos = globalPos;
                    setResizeCursor(cursorTarget(watched), cursorForResizeEdges(edges));
                    mouseEvent->accept();
                    return true;
                }
            }

            if (event->type() == QEvent::MouseMove) {
                if (m_dragging) {
                    const QSize minSize = effectiveMinimumSize(m_window);
                    const QSize maxSize = m_window->maximumSize();
                    m_window->setGeometry(resizedGeometry(m_startGeometry, m_edges, globalPos - m_startGlobalPos,
                                                          minSize, maxSize));
                    mouseEvent->accept();
                    return true;
                }

                const int edges = resizeEdgesAt(m_window, globalPos);
                if (edges != ResizeNone) {
                    setResizeCursor(cursorTarget(watched), cursorForResizeEdges(edges));
                } else {
                    clearResizeCursor();
                }
            }

            if (event->type() == QEvent::MouseButtonRelease && mouseEvent->button() == Qt::LeftButton) {
                m_dragging = false;
                m_edges = ResizeNone;
            }
        }

        if (event->type() == QEvent::Leave && !m_dragging) {
            clearResizeCursor();
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* cursorTarget(QObject* watched) const {
        if (auto* widget = qobject_cast<QWidget*>(watched)) return widget;
        return m_window;
    }

    void setResizeCursor(QWidget* widget, Qt::CursorShape shape) {
        if (!widget) widget = m_window;
        if (m_cursorWidget && m_cursorWidget != widget) m_cursorWidget->unsetCursor();
        if (widget) {
            widget->setCursor(shape);
            m_cursorWidget = widget;
        }
    }

    void clearResizeCursor() {
        if (m_cursorWidget) m_cursorWidget->unsetCursor();
        m_cursorWidget.clear();
    }

    QPointer<QMainWindow> m_window;
    QPointer<QWidget> m_cursorWidget;
    bool m_dragging = false;
    int m_edges = ResizeNone;
    QRect m_startGeometry;
    QPoint m_startGlobalPos;
};

class WindowScreenGuard : public QObject {
public:
    explicit WindowScreenGuard(QWidget* window) : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_window && event->type() == QEvent::Show) {
            QTimer::singleShot(0, this, [this] {
                if (m_window) keepWindowOnScreen(m_window);
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> m_window;
};

void installWindowScreenGuard(QWidget* window) {
    if (!window || window->property(kScreenGuardInstalledProperty).toBool()) return;
    auto* guard = new WindowScreenGuard(window);
    window->installEventFilter(guard);
    window->setProperty(kScreenGuardInstalledProperty, true);
}

void drawClassicRaisedFrame(QPainter& painter, const QRect& rect, bool sunken) {
    const QColor light = Qt::white;
    const QColor mid(128, 128, 128);
    const QColor dark(64, 64, 64);

    const QColor topLeft = sunken ? dark : light;
    const QColor bottomRight = sunken ? light : dark;
    painter.setPen(topLeft);
    painter.drawLine(rect.topLeft(), rect.topRight());
    painter.drawLine(rect.topLeft(), rect.bottomLeft());
    painter.setPen(bottomRight);
    painter.drawLine(rect.bottomLeft(), rect.bottomRight());
    painter.drawLine(rect.topRight(), rect.bottomRight());

    const QRect inner = rect.adjusted(1, 1, -1, -1);
    painter.setPen(sunken ? mid : QColor(223, 223, 223));
    painter.drawLine(inner.topLeft(), inner.topRight());
    painter.drawLine(inner.topLeft(), inner.bottomLeft());
    painter.setPen(sunken ? QColor(223, 223, 223) : mid);
    painter.drawLine(inner.bottomLeft(), inner.bottomRight());
    painter.drawLine(inner.topRight(), inner.bottomRight());
}

void drawCaptionGlyph(QPainter& painter, CaptionCommand command, const QRect& rect, bool closeGlyphIsWhite) {
    QPen pen(closeGlyphIsWhite ? Qt::white : Qt::black);
    pen.setWidth(2);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const int cx = rect.center().x();
    const int cy = rect.center().y();
    if (command == CaptionCommand::Minimize) {
        painter.drawLine(rect.left() + 5, rect.bottom() - 5, rect.right() - 5, rect.bottom() - 5);
        return;
    }
    if (command == CaptionCommand::Close) {
        painter.drawLine(cx - 4, cy - 4, cx + 4, cy + 4);
        painter.drawLine(cx + 4, cy - 4, cx - 4, cy + 4);
        return;
    }
    if (command == CaptionCommand::FloatRestore) {
        painter.drawRect(cx - 4, cy - 4, 8, 8);
        painter.drawLine(cx - 2, cy - 6, cx + 6, cy - 6);
        painter.drawLine(cx + 6, cy - 6, cx + 6, cy + 2);
        return;
    }
    painter.drawRect(cx - 5, cy - 5, 10, 10);
    painter.drawLine(cx - 5, cy - 2, cx + 5, cy - 2);
}

class RetroCaptionButton : public QAbstractButton {
public:
    explicit RetroCaptionButton(CaptionCommand command, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_command(command) {
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setToolTip(toolTipForCommand(command));
    }

    QSize sizeHint() const override {
        return isXp() ? QSize(22, 20) : QSize(18, 16);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, isXp());
        const QRect r = rect().adjusted(1, 1, -1, -1);
        const bool down = isDown();
        const bool hover = underMouse();

        if (!isXp()) {
            painter.fillRect(rect(), QColor(212, 208, 200));
            drawClassicRaisedFrame(painter, r, down);
            drawCaptionGlyph(painter, m_command, r.adjusted(down ? 1 : 0, down ? 1 : 0, down ? 1 : 0, down ? 1 : 0),
                             false);
            return;
        }

        const bool close = m_command == CaptionCommand::Close;
        const QColor top = close ? QColor(255, 130, 102) : QColor(124, 184, 255);
        const QColor bottom = close ? QColor(194, 30, 22) : QColor(0, 84, 227);
        const QColor hotTop = close ? QColor(255, 167, 128) : QColor(160, 209, 255);
        const QColor hotBottom = close ? QColor(230, 54, 38) : QColor(32, 112, 245);

        QLinearGradient gradient(r.topLeft(), r.bottomLeft());
        gradient.setColorAt(0.0, hover ? hotTop : top);
        gradient.setColorAt(1.0, hover ? hotBottom : bottom);
        painter.setPen(QColor(255, 255, 255, 180));
        painter.setBrush(gradient);
        painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 4, 4);
        if (down) painter.fillRect(r.adjusted(2, 2, -2, -2), QColor(0, 0, 0, 45));
        drawCaptionGlyph(painter, m_command, r.adjusted(down ? 1 : 0, down ? 1 : 0, down ? 1 : 0, down ? 1 : 0),
                         true);
    }

private:
    static QString toolTipForCommand(CaptionCommand command) {
        switch (command) {
            case CaptionCommand::Minimize:
                return QObject::tr("最小化");
            case CaptionCommand::MaximizeRestore:
                return QObject::tr("最大化/元に戻す");
            case CaptionCommand::FloatRestore:
                return QObject::tr("別ウィンドウ/戻す");
            case CaptionCommand::Close:
                return QObject::tr("閉じる");
        }
        return {};
    }

    CaptionCommand m_command;
};

class RetroTitleBarBase : public QWidget {
public:
    explicit RetroTitleBarBase(QWidget* parent = nullptr) : QWidget(parent) {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_StyledBackground, false);
        setMinimumHeight(isXp() ? 28 : 23);
    }

protected:
    void paintTitleBackground(QPainter& painter, const QString& title, bool active) {
        QRect r = rect();
        if (isXp()) {
            QLinearGradient gradient(r.topLeft(), r.topRight());
            gradient.setColorAt(0.0, titleBarStartColor(active));
            gradient.setColorAt(1.0, titleBarEndColor(active));
            painter.fillRect(r, gradient);
            painter.setPen(QColor(255, 255, 255, 80));
            painter.drawLine(r.left(), r.top(), r.right(), r.top());
        } else {
            QLinearGradient gradient(r.topLeft(), r.topRight());
            gradient.setColorAt(0.0, titleBarStartColor(active));
            gradient.setColorAt(1.0, titleBarEndColor(active));
            painter.fillRect(r.adjusted(2, 2, -2, -2), gradient);
            drawClassicRaisedFrame(painter, r.adjusted(0, 0, -1, -1), false);
        }

        QFont titleFont = font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(Qt::white);
        const QRect textRect = r.adjusted(isXp() ? 8 : 7, 0, 88, 0);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);
    }
};

class RetroDockTitleBar : public RetroTitleBarBase {
public:
    explicit RetroDockTitleBar(QDockWidget* dock) : RetroTitleBarBase(dock), m_dock(dock) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(isXp() ? 7 : 5, isXp() ? 4 : 3, isXp() ? 5 : 4, isXp() ? 4 : 3);
        layout->setSpacing(2);
        layout->addStretch(1);

        auto* minButton = new RetroCaptionButton(CaptionCommand::Minimize, this);
        auto* floatButton = new RetroCaptionButton(CaptionCommand::FloatRestore, this);
        auto* closeButton = new RetroCaptionButton(CaptionCommand::Close, this);
        layout->addWidget(minButton);
        layout->addWidget(floatButton);
        layout->addSpacing(isXp() ? 2 : 4);
        layout->addWidget(closeButton);

        connect(minButton, &QAbstractButton::clicked, dock, [dock] { dock->hide(); });
        connect(floatButton, &QAbstractButton::clicked, dock, [dock] { dock->setFloating(!dock->isFloating()); });
        connect(closeButton, &QAbstractButton::clicked, dock, [dock] { dock->close(); });
        connect(dock, &QWidget::windowTitleChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        paintTitleBackground(painter, m_dock ? m_dock->windowTitle() : QString(), true);
    }

private:
    QDockWidget* m_dock = nullptr;
};

class RetroWindowTitleBar : public RetroTitleBarBase {
public:
    explicit RetroWindowTitleBar(QMainWindow* window) : RetroTitleBarBase(window), m_window(window) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(isXp() ? 8 : 5, isXp() ? 4 : 3, isXp() ? 5 : 4, isXp() ? 4 : 3);
        layout->setSpacing(2);
        layout->addStretch(1);

        auto* minButton = new RetroCaptionButton(CaptionCommand::Minimize, this);
        auto* maxButton = new RetroCaptionButton(CaptionCommand::MaximizeRestore, this);
        auto* closeButton = new RetroCaptionButton(CaptionCommand::Close, this);
        layout->addWidget(minButton);
        layout->addWidget(maxButton);
        layout->addSpacing(isXp() ? 2 : 4);
        layout->addWidget(closeButton);

        connect(minButton, &QAbstractButton::clicked, window, [window] { window->showMinimized(); });
        connect(maxButton, &QAbstractButton::clicked, window, [window] {
            window->isMaximized() ? window->showNormal() : window->showMaximized();
        });
        connect(closeButton, &QAbstractButton::clicked, window, [window] { window->close(); });
        connect(window, &QWidget::windowTitleChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        paintTitleBackground(painter, m_window ? m_window->windowTitle() : QString(), isActiveWindow());
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_window) {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
            event->accept();
            return;
        }
        RetroTitleBarBase::mouseDoubleClickEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_window) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - m_window->frameGeometry().topLeft();
            event->accept();
            return;
        }
        RetroTitleBarBase::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging && m_window && !m_window->isMaximized()) {
            m_window->move(event->globalPosition().toPoint() - m_dragOffset);
            event->accept();
            return;
        }
        RetroTitleBarBase::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        m_dragging = false;
        RetroTitleBarBase::mouseReleaseEvent(event);
    }

private:
    QMainWindow* m_window = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

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
QMenu::separator {
    height: 2px;
    background: #808080;
    border-bottom: 1px solid #ffffff;
    margin: 3px 4px;
}
QMenu::indicator {
    width: 13px;
    height: 13px;
}
QMenu::indicator:checked {
    background: #d4d0c8;
    border-top: 1px solid #404040;
    border-left: 1px solid #404040;
    border-right: 1px solid #ffffff;
    border-bottom: 1px solid #ffffff;
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
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #000000;
    selection-background-color: #000080;
    selection-color: #ffffff;
    border-top: 2px solid #404040;
    border-left: 2px solid #404040;
    border-right: 2px solid #ffffff;
    border-bottom: 2px solid #ffffff;
    outline: 0;
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
QMenu::separator {
    height: 1px;
    background: #808080;
    margin: 4px 5px;
}
QMenu::indicator {
    width: 14px;
    height: 14px;
}
QMenu::indicator:checked {
    background-color: #316ac5;
    border: 1px solid #0a246a;
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
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 20px;
}
QComboBox:hover {
    border: 1px solid #f2a300;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #000000;
    selection-background-color: #316ac5;
    selection-color: #ffffff;
    border: 1px solid #7f9db9;
    outline: 0;
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
    captureStandardAppearance(app);
    app.setProperty(kEnabledProperty, true);
    app.setProperty(kVariantProperty, variant == RetroThemeVariant::Windows95 ? 95 : 1);
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

void clearRetroTheme(QApplication& app) {
    app.setProperty(kEnabledProperty, false);
    app.setProperty(kVariantProperty, 0);
    restoreStandardAppearance(app);
}

void setRetroThemeAvailable(QApplication& app, bool available) {
    app.setProperty(kAvailableProperty, available);
}

bool isRetroThemeAvailable() {
    return qApp && qApp->property(kAvailableProperty).toBool();
}

bool isRetroThemeEnabled() {
    return qApp && qApp->property(kEnabledProperty).toBool();
}

RetroThemeVariant activeRetroThemeVariant() {
    if (!qApp) return RetroThemeVariant::Windows95;
    return qApp->property(kVariantProperty).toInt() == 95 ? RetroThemeVariant::Windows95
                                                           : RetroThemeVariant::WindowsXp;
}

void installRetroDockTitleBars(QWidget* root) {
    if (!root || !isRetroThemeEnabled()) return;
    const auto docks = root->findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (!dock || dock->property(kDockTitleInstalledProperty).toBool()) continue;
        dock->setTitleBarWidget(new RetroDockTitleBar(dock));
        dock->setProperty(kDockTitleInstalledProperty, true);
    }
}

void removeRetroDockTitleBars(QWidget* root) {
    if (!root) return;
    const auto docks = root->findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (!dock || !dock->property(kDockTitleInstalledProperty).toBool()) continue;
        dock->setTitleBarWidget(nullptr);
        dock->setProperty(kDockTitleInstalledProperty, false);
        dock->update();
    }
}

void installRetroWindowFrame(QMainWindow* window) {
    if (!window) return;
    installWindowScreenGuard(window);
    if (!isRetroThemeEnabled()) return;

    const bool wasVisible = window->isVisible();
    if (!window->property(kWindowFrameInstalledProperty).toBool()) {
        window->setMenuWidget(new RetroWindowTitleBar(window));
        window->setWindowFlag(Qt::FramelessWindowHint, true);
        window->setProperty(kWindowFrameInstalledProperty, true);
    }
    auto* filter = dynamic_cast<RetroResizeFilter*>(window->property(kResizeFilterInstalledProperty).value<QObject*>());
    if (!filter) {
        filter = new RetroResizeFilter(window);
        window->setProperty(kResizeFilterInstalledProperty, QVariant::fromValue(static_cast<QObject*>(filter)));
    }
    filter->installOn(window);
    if (wasVisible) window->show();
}

void removeRetroWindowFrame(QMainWindow* window) {
    if (!window) return;

    const bool wasVisible = window->isVisible();
    if (window->property(kWindowFrameInstalledProperty).toBool()) {
        window->setMenuWidget(nullptr);
        window->setWindowFlag(Qt::FramelessWindowHint, false);
        window->setProperty(kWindowFrameInstalledProperty, false);
        window->unsetCursor();
    }
    if (wasVisible) window->show();
}

void keepWindowOnScreen(QWidget* window) {
    if (!window || window->isMaximized() || window->isFullScreen()) return;

    const QRect available = availableScreenGeometryFor(window);
    if (available.isEmpty()) return;

    QRect frame = window->frameGeometry();
    if (frame.isEmpty()) frame = QRect(window->pos(), window->size());

    const QSize minSize = effectiveMinimumSize(window).boundedTo(available.size());
    const QSize maxSize = effectiveMaximumSize(window, available).expandedTo(minSize);
    const QSize targetSize = frame.size().expandedTo(minSize).boundedTo(maxSize);

    if (targetSize != frame.size()) window->resize(targetSize);

    frame = window->frameGeometry();
    const QPoint topLeft = clampedTopLeft(frame, available);
    if (topLeft != frame.topLeft()) window->move(window->pos() + (topLeft - frame.topLeft()));
}

}  // namespace perapera::ui
