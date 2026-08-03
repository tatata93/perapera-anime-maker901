#include "DrawingFocusPalette.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "ui/BrushWidthControl.h"

namespace {

QToolButton* makeButton(QWidget* parent, const QString& text, const QString& toolTip = {}) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumWidth(0);
    button->setFixedHeight(23);
    if (!toolTip.isEmpty()) button->setToolTip(toolTip);
    return button;
}

}  // namespace

DrawingFocusPalette::DrawingFocusPalette(QWidget* parent)
    : QMainWindow(parent, Qt::Tool | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                             Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint) {
    setWindowTitle(tr("作画集中"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumWidth(0);
    resize(560, 96);

    auto* content = new QWidget(this);
    setCentralWidget(content);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(3, 3, 3, 3);
    root->setSpacing(2);

    auto* commandRow = new QHBoxLayout();
    commandRow->setSpacing(2);
    auto* normalButton = makeButton(this, tr("通常"), tr("作画集中モードを終了"));
    commandRow->addWidget(normalButton);
    commandRow->addSpacing(4);

    m_undoButton = makeButton(this, tr("戻す"), tr("元に戻す"));
    m_undoButton->setObjectName(QStringLiteral("FocusUndoButton"));
    m_redoButton = makeButton(this, tr("進む"), tr("やり直す"));
    m_redoButton->setObjectName(QStringLiteral("FocusRedoButton"));
    commandRow->addWidget(m_undoButton);
    commandRow->addWidget(m_redoButton);
    commandRow->addStretch();

    auto* previousButton = makeButton(this, tr("前"), tr("前のコマ (A)"));
    auto* nextButton = makeButton(this, tr("次"), tr("次のコマ (D)"));
    m_frameLabel = new QLabel(this);
    m_frameLabel->setAlignment(Qt::AlignCenter);
    m_frameLabel->setMinimumWidth(48);
    commandRow->addWidget(previousButton);
    commandRow->addWidget(m_frameLabel);
    commandRow->addWidget(nextButton);
    root->addLayout(commandRow);

    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(2);
    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    toolRow->addWidget(new QLabel(tr("道具"), this));
    for (const auto& [text, id] :
         {std::pair{tr("ペン"), 0}, std::pair{tr("線"), 1},
          std::pair{tr("消"), 2}, std::pair{tr("塗"), 3},
          std::pair{tr("縄"), 4}, std::pair{tr("スポ"), 6}}) {
        toolRow->addWidget(addToolButton(text, id));
    }
    m_navigationButton = makeButton(this, tr("手"), tr("ペン先ドラッグでキャンバスを移動"));
    m_navigationButton->setCheckable(true);
    m_navigationButton->setObjectName(QStringLiteral("FocusNavigationButton"));
    toolRow->addWidget(m_navigationButton);
    toolRow->addSpacing(4);

    toolRow->addWidget(new QLabel(tr("線幅"), this));
    m_widthSlider = new QSlider(Qt::Horizontal, this);
    m_widthSlider->setObjectName(QStringLiteral("FocusBrushWidthSlider"));
    m_widthSlider->setRange(perapera::ui::kBrushWidthSliderMin,
                            perapera::ui::kBrushWidthSliderMax);
    m_widthSlider->setFixedWidth(92);
    m_widthSlider->setFocusPolicy(Qt::ClickFocus);
    toolRow->addWidget(m_widthSlider);

    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setObjectName(QStringLiteral("FocusBrushWidthSpin"));
    m_widthSpin->setRange(perapera::ui::kBrushWidthMin, perapera::ui::kBrushWidthMax);
    m_widthSpin->setSingleStep(perapera::ui::kBrushWidthStep);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setSuffix(tr(" px"));
    m_widthSpin->setKeyboardTracking(false);
    m_widthSpin->setFixedWidth(74);
    toolRow->addWidget(m_widthSpin);

    m_colorButton = makeButton(this, QString(), tr("ペンの色"));
    m_colorButton->setObjectName(QStringLiteral("FocusColorButton"));
    m_colorButton->setFixedSize(24, 23);
    toolRow->addWidget(m_colorButton);
    toolRow->addStretch();
    root->addLayout(toolRow);

    auto* viewRow = new QHBoxLayout();
    viewRow->setSpacing(2);
    viewRow->addWidget(new QLabel(tr("表示"), this));
    auto* zoomOutButton = makeButton(this, QStringLiteral("-"), tr("縮小"));
    auto* zoomInButton = makeButton(this, QStringLiteral("+"), tr("拡大"));
    m_zoomSpin = new QDoubleSpinBox(this);
    m_zoomSpin->setObjectName(QStringLiteral("FocusZoomSpin"));
    m_zoomSpin->setRange(5.0, 3200.0);
    m_zoomSpin->setSingleStep(10.0);
    m_zoomSpin->setDecimals(0);
    m_zoomSpin->setSuffix(tr(" %"));
    m_zoomSpin->setKeyboardTracking(false);
    m_zoomSpin->setFixedWidth(70);
    viewRow->addWidget(zoomOutButton);
    viewRow->addWidget(m_zoomSpin);
    viewRow->addWidget(zoomInButton);
    viewRow->addSpacing(4);

    auto* rotateLeftButton = makeButton(this, tr("左"), tr("15度左へ回転"));
    auto* rotateRightButton = makeButton(this, tr("右"), tr("15度右へ回転"));
    m_rotationSpin = new QDoubleSpinBox(this);
    m_rotationSpin->setObjectName(QStringLiteral("FocusRotationSpin"));
    m_rotationSpin->setRange(-180.0, 180.0);
    m_rotationSpin->setSingleStep(5.0);
    m_rotationSpin->setDecimals(0);
    m_rotationSpin->setSuffix(tr(" 度"));
    m_rotationSpin->setKeyboardTracking(false);
    m_rotationSpin->setFixedWidth(72);
    viewRow->addWidget(rotateLeftButton);
    viewRow->addWidget(m_rotationSpin);
    viewRow->addWidget(rotateRightButton);

    auto* resetButton = makeButton(this, tr("全体"), tr("拡大・回転・位置をリセット"));
    viewRow->addWidget(resetButton);
    viewRow->addStretch();
    root->addLayout(viewRow);

    connect(normalButton, &QToolButton::clicked, this,
            &DrawingFocusPalette::returnToNormalRequested);
    connect(m_undoButton, &QToolButton::clicked, this, &DrawingFocusPalette::undoRequested);
    connect(m_redoButton, &QToolButton::clicked, this, &DrawingFocusPalette::redoRequested);
    connect(previousButton, &QToolButton::clicked, this,
            &DrawingFocusPalette::previousFrameRequested);
    connect(nextButton, &QToolButton::clicked, this,
            &DrawingFocusPalette::nextFrameRequested);
    connect(m_toolGroup, &QButtonGroup::idClicked, this, &DrawingFocusPalette::toolRequested);
    connect(m_navigationButton, &QToolButton::toggled, this,
            &DrawingFocusPalette::navigationModeChanged);
    connect(m_widthSlider, &QSlider::valueChanged, this, [this](int value) {
        const double width =
            perapera::ui::brushWidthFromRadius(perapera::ui::brushRadiusFromSliderValue(value));
        const QSignalBlocker blocker(m_widthSpin);
        m_widthSpin->setValue(width);
        emit brushWidthChanged(width);
    });
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, [this](double width) {
        const QSignalBlocker blocker(m_widthSlider);
        m_widthSlider->setValue(perapera::ui::sliderValueFromBrushRadius(
            perapera::ui::brushRadiusFromWidth(width)));
        emit brushWidthChanged(width);
    });
    connect(m_colorButton, &QToolButton::clicked, this, &DrawingFocusPalette::colorRequested);
    connect(m_zoomSpin, &QDoubleSpinBox::valueChanged, this,
            &DrawingFocusPalette::zoomChanged);
    connect(m_rotationSpin, &QDoubleSpinBox::valueChanged, this,
            &DrawingFocusPalette::rotationChanged);
    connect(zoomOutButton, &QToolButton::clicked, this,
            [this] { m_zoomSpin->setValue(m_zoomSpin->value() / 1.25); });
    connect(zoomInButton, &QToolButton::clicked, this,
            [this] { m_zoomSpin->setValue(m_zoomSpin->value() * 1.25); });
    connect(rotateLeftButton, &QToolButton::clicked, this,
            [this] { m_rotationSpin->setValue(m_rotationSpin->value() - 15.0); });
    connect(rotateRightButton, &QToolButton::clicked, this,
            [this] { m_rotationSpin->setValue(m_rotationSpin->value() + 15.0); });
    connect(resetButton, &QToolButton::clicked, this,
            &DrawingFocusPalette::viewResetRequested);

    setActiveTool(0);
    setBrushWidth(12.0);
    setPenColor(Qt::black);
    setView(1.0f, 0.0);
    setFrame(0, 1);
}

QToolButton* DrawingFocusPalette::addToolButton(const QString& text, int toolId) {
    QToolButton* button = makeButton(this, text);
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setObjectName(QStringLiteral("FocusTool%1").arg(toolId));
    m_toolGroup->addButton(button, toolId);
    return button;
}

void DrawingFocusPalette::setActiveTool(int tool) {
    if (QAbstractButton* button = m_toolGroup->button(tool)) {
        const QSignalBlocker blocker(m_toolGroup);
        button->setChecked(true);
    }
}

void DrawingFocusPalette::setNavigationMode(bool enabled) {
    const QSignalBlocker blocker(m_navigationButton);
    m_navigationButton->setChecked(enabled);
}

void DrawingFocusPalette::setBrushWidth(double width) {
    const QSignalBlocker spinBlocker(m_widthSpin);
    const QSignalBlocker sliderBlocker(m_widthSlider);
    m_widthSpin->setValue(width);
    m_widthSlider->setValue(perapera::ui::sliderValueFromBrushRadius(
        perapera::ui::brushRadiusFromWidth(width)));
}

void DrawingFocusPalette::setPenColor(const QColor& color) {
    m_colorButton->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(color.name(QColor::HexArgb)));
}

void DrawingFocusPalette::setView(float zoom, qreal rotationDeg) {
    const QSignalBlocker zoomBlocker(m_zoomSpin);
    const QSignalBlocker rotationBlocker(m_rotationSpin);
    m_zoomSpin->setValue(static_cast<double>(zoom) * 100.0);
    m_rotationSpin->setValue(rotationDeg);
}

void DrawingFocusPalette::setFrame(size_t currentFrame, size_t frameCount) {
    const size_t count = std::max<size_t>(1, frameCount);
    m_frameLabel->setText(tr("%1 / %2").arg(currentFrame + 1).arg(count));
}

void DrawingFocusPalette::setUndoRedoEnabled(bool undoEnabled, bool redoEnabled) {
    m_undoButton->setEnabled(undoEnabled);
    m_redoButton->setEnabled(redoEnabled);
}

void DrawingFocusPalette::closeEvent(QCloseEvent* event) {
    emit returnToNormalRequested();
    event->ignore();
}

void DrawingFocusPalette::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (qApp) qApp->installEventFilter(this);
}

void DrawingFocusPalette::hideEvent(QHideEvent* event) {
    if (qApp) qApp->removeEventFilter(this);
    QMainWindow::hideEvent(event);
}

bool DrawingFocusPalette::eventFilter(QObject* watched, QEvent* event) {
    if (isVisible() && event->type() == QEvent::KeyPress) {
        QWidget* focus = QApplication::focusWidget();
        QWidget* active = QApplication::activeWindow();
        if (active == this || (focus && focus->window() == this)) {
            if (handleShortcutKey(static_cast<QKeyEvent*>(event))) return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool DrawingFocusPalette::handleShortcutKey(QKeyEvent* event) {
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool plain = event->modifiers() == Qt::NoModifier;

    if (ctrl && event->key() == Qt::Key_Z) {
        emit undoRequested();
        return true;
    }
    if (ctrl && event->key() == Qt::Key_Y) {
        emit redoRequested();
        return true;
    }
    if (!plain) return false;

    switch (event->key()) {
        case Qt::Key_Tab:
            emit returnToNormalRequested();
            return true;
        case Qt::Key_A:
            emit previousFrameRequested();
            return true;
        case Qt::Key_D:
            emit nextFrameRequested();
            return true;
        case Qt::Key_B:
            emit toolRequested(0);
            return true;
        case Qt::Key_N:
            emit toolRequested(1);
            return true;
        case Qt::Key_E:
            emit toolRequested(2);
            return true;
        case Qt::Key_G:
            emit toolRequested(3);
            return true;
        case Qt::Key_L:
            emit toolRequested(4);
            return true;
        case Qt::Key_I:
            emit toolRequested(6);
            return true;
        default:
            return false;
    }
}
