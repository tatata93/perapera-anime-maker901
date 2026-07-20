#include "SettingBoardWindow.h"

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTextOption>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Project.h"
#include "render/GLCanvas.h"
#include "ui/CanvasSizeDialog.h"
#include "ui/FloatingCanvasWindow.h"
#include "ui/LayerPanel.h"
#include "ui/PaintLayerUtils.h"
#include "ui/RetroTheme.h"

namespace {
// 設定ボード1枚のサイズ(作画キャンバスと同じ1920x1080)
constexpr int kBoardWidth = 1920;
constexpr int kBoardHeight = 1080;

// 太さスライダーの範囲(ペン/消しゴム共通)
constexpr int kRadiusMin = 1;
constexpr int kRadiusMax = 40;

// 色指定リストのスウォッチアイコンの一辺サイズ
constexpr int kColorSpecSwatchSize = 16;

// 画像を、既存の絵の上にsrc-over合成する。画像はboardサイズ内へアスペクト維持で
// 最大フィットするよう拡大縮小し、中央配置する。合成はboardの透明度も考慮した
// straight-alphaのsrc-over(BrushEngineの合成式と同じ考え方)で行う
void pasteImageOntoBoard(core::Bitmap& board, const QImage& source) {
    if (board.isEmpty() || source.isNull()) return;

    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    const QImage scaled =
        rgba.scaled(board.width(), board.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int offsetX = (board.width() - scaled.width()) / 2;
    const int offsetY = (board.height() - scaled.height()) / 2;

    for (int y = 0; y < scaled.height(); ++y) {
        const int by = offsetY + y;
        if (by < 0 || by >= board.height()) continue;
        const uchar* row = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const int bx = offsetX + x;
            if (bx < 0 || bx >= board.width()) continue;

            const uchar* px = row + x * 4;
            const core::Bitmap::Pixel src{px[0], px[1], px[2], px[3]};
            if (src.a == 0) continue;

            core::Bitmap::Pixel dst = board.pixel(bx, by);
            const float srcA = src.a / 255.0f;
            const float dstA = dst.a / 255.0f;
            const float outA = srcA + dstA * (1.0f - srcA);
            if (outA > 0.0f) {
                const auto blend = [srcA, dstA, outA](uint8_t s, uint8_t d) {
                    return static_cast<uint8_t>(std::lround((s * srcA + d * dstA * (1.0f - srcA)) / outA));
                };
                dst.r = blend(src.r, dst.r);
                dst.g = blend(src.g, dst.g);
                dst.b = blend(src.b, dst.b);
            }
            dst.a = static_cast<uint8_t>(std::lround(outA * 255.0f));
            board.setPixel(bx, by, dst);
        }
    }
}

void resizeBitmapCentered(core::Bitmap& bitmap, int width, int height) {
    core::Bitmap resized(width, height);
    resized.fill({0, 0, 0, 0});
    if (!bitmap.isEmpty()) {
        const int copyW = std::min(bitmap.width(), width);
        const int copyH = std::min(bitmap.height(), height);
        const int srcX = std::max(0, (bitmap.width() - width) / 2);
        const int srcY = std::max(0, (bitmap.height() - height) / 2);
        const int dstX = std::max(0, (width - bitmap.width()) / 2);
        const int dstY = std::max(0, (height - bitmap.height()) / 2);
        for (int y = 0; y < copyH; ++y) {
            for (int x = 0; x < copyW; ++x) {
                resized.setPixel(dstX + x, dstY + y, bitmap.pixel(srcX + x, srcY + y));
            }
        }
    }
    bitmap = std::move(resized);
}

QImage makeFinalStampOverlay(int width, int height) {
    QImage overlay(width, height, QImage::Format_RGBA8888);
    overlay.fill(Qt::transparent);
    if (width <= 0 || height <= 0) return overlay;

    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int shortSide = std::min(width, height);
    const int stampW = std::clamp(width / 5, 180, 620);
    const int stampH = std::clamp(height / 10, 70, 220);
    const int margin = std::clamp(shortSide / 28, 28, 160);
    const QRectF rect(width - stampW - margin, height - stampH - margin, stampW, stampH);

    QPen pen(QColor(190, 24, 24, 230), std::max(4, shortSide / 260));
    painter.setPen(pen);
    painter.setBrush(QColor(255, 255, 255, 28));
    painter.drawRoundedRect(rect, 6, 6);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(std::clamp(stampH / 2, 30, 96));
    painter.setFont(font);
    painter.setPen(QColor(190, 24, 24, 235));
    painter.drawText(rect, Qt::AlignCenter, QStringLiteral("決定稿"));
    return overlay;
}

QColor colorFromPixel(core::Bitmap::Pixel pixel) {
    return QColor(pixel.r, pixel.g, pixel.b, pixel.a);
}

core::Bitmap::Pixel pixelFromColor(const QColor& color) {
    return {static_cast<uint8_t>(color.red()), static_cast<uint8_t>(color.green()),
            static_cast<uint8_t>(color.blue()), static_cast<uint8_t>(color.alpha())};
}

QRect textBoxRect(const core::SettingBoardTextBox& box, int width, int height) {
    if (width <= 0 || height <= 0) return QRect();
    const int boxW = std::clamp(box.width, 1, width);
    const int boxH = std::clamp(box.height, 1, height);
    const int x = std::clamp(box.x, 0, std::max(0, width - boxW));
    const int y = std::clamp(box.y, 0, std::max(0, height - boxH));
    return QRect(x, y, boxW, boxH);
}

void drawBoardTextBoxes(QPainter& painter, const std::vector<core::SettingBoardTextBox>& boxes, int width,
                        int height) {
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);

    for (const core::SettingBoardTextBox& box : boxes) {
        if (box.text.empty()) continue;
        const QRect rect = textBoxRect(box, width, height);
        if (rect.isEmpty()) continue;

        QFont font = painter.font();
        font.setPixelSize(std::clamp(box.fontPixelSize, 1, std::max(1, height)));
        painter.setFont(font);
        painter.setPen(colorFromPixel(box.color));
        painter.drawText(rect.adjusted(6, 4, -6, -4), QString::fromStdString(box.text), option);
    }
}

QImage makeBoardDecorationsOverlay(const core::SettingBoard& board, int width, int height) {
    QImage overlay(width, height, QImage::Format_RGBA8888);
    overlay.fill(Qt::transparent);
    if (width <= 0 || height <= 0) return overlay;

    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    drawBoardTextBoxes(painter, board.textBoxes, width, height);
    if (board.finalStamp) painter.drawImage(0, 0, makeFinalStampOverlay(width, height));
    painter.end();
    return overlay;
}

bool hasBoardDecorations(const core::SettingBoard& board) {
    if (board.finalStamp) return true;
    return std::any_of(board.textBoxes.begin(), board.textBoxes.end(),
                       [](const core::SettingBoardTextBox& box) { return !box.text.empty(); });
}

QString textBoxPreview(const core::SettingBoardTextBox& box, int index) {
    QString text = QString::fromStdString(box.text).simplified();
    if (text.isEmpty()) text = QObject::tr("(空の文字)");
    if (text.size() > 24) text = text.left(24) + QStringLiteral("...");
    return QObject::tr("%1: %2").arg(index + 1).arg(text);
}

bool editSettingBoardTextBox(QWidget* parent, const QSize& canvasSize, core::SettingBoardTextBox* box) {
    if (!box) return false;

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("文字ボックス"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* textEdit = new QPlainTextEdit(&dialog);
    textEdit->setPlainText(QString::fromStdString(box->text));
    textEdit->setMinimumHeight(100);
    layout->addWidget(new QLabel(QObject::tr("文字"), &dialog));
    layout->addWidget(textEdit);

    auto* form = new QFormLayout();
    auto* xSpin = new QSpinBox(&dialog);
    auto* ySpin = new QSpinBox(&dialog);
    auto* widthSpin = new QSpinBox(&dialog);
    auto* heightSpin = new QSpinBox(&dialog);
    auto* sizeSpin = new QSpinBox(&dialog);
    const int canvasW = std::max(1, canvasSize.width());
    const int canvasH = std::max(1, canvasSize.height());
    xSpin->setRange(0, canvasW - 1);
    ySpin->setRange(0, canvasH - 1);
    widthSpin->setRange(1, canvasW);
    heightSpin->setRange(1, canvasH);
    sizeSpin->setRange(6, std::max(6, canvasH));
    widthSpin->setValue(std::clamp(box->width, 1, canvasW));
    heightSpin->setValue(std::clamp(box->height, 1, canvasH));
    xSpin->setValue(std::clamp(box->x, 0, std::max(0, canvasW - widthSpin->value())));
    ySpin->setValue(std::clamp(box->y, 0, std::max(0, canvasH - heightSpin->value())));
    sizeSpin->setValue(std::clamp(box->fontPixelSize, 6, std::max(6, canvasH)));

    QColor textColor = colorFromPixel(box->color);
    auto* colorButton = new QPushButton(QObject::tr("色を選ぶ"), &dialog);
    const auto applyColorButton = [&] {
        colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(textColor.name()));
    };
    applyColorButton();
    QObject::connect(colorButton, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(textColor, &dialog, QObject::tr("文字の色"));
        if (!chosen.isValid()) return;
        textColor = chosen;
        applyColorButton();
    });

    form->addRow(QObject::tr("左"), xSpin);
    form->addRow(QObject::tr("上"), ySpin);
    form->addRow(QObject::tr("幅"), widthSpin);
    form->addRow(QObject::tr("高さ"), heightSpin);
    form->addRow(QObject::tr("文字サイズ"), sizeSpin);
    form->addRow(QObject::tr("色"), colorButton);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return false;
    const QString text = textEdit->toPlainText();
    if (text.trimmed().isEmpty()) return false;

    box->text = text.toStdString();
    box->x = xSpin->value();
    box->y = ySpin->value();
    box->width = widthSpin->value();
    box->height = heightSpin->value();
    box->fontPixelSize = sizeSpin->value();
    box->color = pixelFromColor(textColor);
    return true;
}

void moveTextBoxesForCenteredResize(std::vector<core::SettingBoardTextBox>& boxes, int oldW, int oldH, int newW,
                                    int newH) {
    const int dx = (newW - oldW) / 2;
    const int dy = (newH - oldH) / 2;
    for (core::SettingBoardTextBox& box : boxes) {
        box.width = std::clamp(box.width, 1, std::max(1, newW));
        box.height = std::clamp(box.height, 1, std::max(1, newH));
        box.x = std::clamp(box.x + dx, 0, std::max(0, newW - box.width));
        box.y = std::clamp(box.y + dy, 0, std::max(0, newH - box.height));
        box.fontPixelSize = std::clamp(box.fontPixelSize, 1, std::max(1, newH));
    }
}

QSize boardCanvasSize(const core::SettingBoard& board) {
    return perapera::ui::paintLayerCanvasSize(board.layers, board.image, kBoardWidth, kBoardHeight);
}

void syncSettingBoardComposite(core::SettingBoard& board) {
    const QSize size = boardCanvasSize(board);
    perapera::ui::ensurePaintLayers(board.layers, board.activeLayer, board.image, size.width(), size.height(), true);
    board.image = perapera::ui::compositePaintLayers(board.layers, size.width(), size.height());
}

QImage boardExportImage(core::SettingBoard& board) {
    syncSettingBoardComposite(board);
    const QImage boardImage = perapera::ui::bitmapToImageCopy(board.image).convertToFormat(QImage::Format_RGBA8888);
    if (boardImage.isNull()) return QImage();

    QImage image(boardImage.size(), QImage::Format_RGBA8888);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.drawImage(0, 0, boardImage);
        painter.end();
    }
    if (!image.isNull() && hasBoardDecorations(board)) {
        QPainter painter(&image);
        painter.drawImage(0, 0, makeBoardDecorationsOverlay(board, image.width(), image.height()));
        painter.end();
    }
    return image;
}

}  // namespace

SettingBoardWindow::SettingBoardWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("設定ボード - perapera-anime-maker901"));
    resize(1200, 760);

    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);

    // 左: ボード一覧+ボタン列
    auto* leftContainer = new QWidget(central);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(leftContainer);
    leftLayout->addWidget(m_list);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("追加"), leftContainer);
    auto* removeButton = new QPushButton(tr("削除"), leftContainer);
    auto* renameButton = new QPushButton(tr("名前変更"), leftContainer);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addWidget(renameButton);
    leftLayout->addLayout(buttonLayout);
    mainLayout->addWidget(leftContainer, 1);

    // 右: ツール行+描画エリア
    auto* rightContainer = new QWidget(central);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto* toolRow = new QHBoxLayout();
    m_penButton = new QPushButton(tr("ペン"), rightContainer);
    m_eraserButton = new QPushButton(tr("消しゴム"), rightContainer);
    m_eyedropperButton = new QPushButton(tr("スポイト"), rightContainer);
    m_penButton->setCheckable(true);
    m_eraserButton->setCheckable(true);
    m_fillButton = new QPushButton(tr("塗りつぶし"), rightContainer);
    m_fillButton->setCheckable(true);
    m_eyedropperButton->setCheckable(true);
    m_penButton->setAutoExclusive(true);
    m_eraserButton->setAutoExclusive(true);
    m_fillButton->setAutoExclusive(true);
    m_eyedropperButton->setAutoExclusive(true);
    m_penButton->setChecked(true);
    toolRow->addWidget(m_penButton);
    toolRow->addWidget(m_eraserButton);
    toolRow->addWidget(m_fillButton);
    toolRow->addWidget(m_eyedropperButton);

    toolRow->addWidget(new QLabel(tr("太さ"), rightContainer));
    m_radiusSlider = new QSlider(Qt::Horizontal, rightContainer);
    m_radiusSlider->setRange(kRadiusMin, kRadiusMax);
    m_radiusSlider->setValue(static_cast<int>(m_penRadius));
    m_radiusSlider->setFixedWidth(120);
    toolRow->addWidget(m_radiusSlider);
    m_radiusValueLabel = new QLabel(QString::number(static_cast<int>(m_penRadius)), rightContainer);
    m_radiusValueLabel->setFixedWidth(24);
    toolRow->addWidget(m_radiusValueLabel);

    m_colorButton = new QPushButton(tr("色"), rightContainer);
    m_colorButton->setFixedWidth(48);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    toolRow->addWidget(m_colorButton);
    auto* textButton = new QPushButton(tr("文字"), rightContainer);
    toolRow->addWidget(textButton);

    auto* pasteButton = new QPushButton(tr("画像を貼る"), rightContainer);
    toolRow->addWidget(pasteButton);
    auto* resizeButton = new QPushButton(tr("サイズ"), rightContainer);
    toolRow->addWidget(resizeButton);
    auto* exportButton = new QPushButton(tr("PNG書き出し"), rightContainer);
    toolRow->addWidget(exportButton);
    m_finalStampButton = new QPushButton(tr("決定稿"), rightContainer);
    m_finalStampButton->setCheckable(true);
    toolRow->addWidget(m_finalStampButton);
    auto* detachButton = new QPushButton(tr("別窓"), rightContainer);
    toolRow->addWidget(detachButton);

    toolRow->addStretch();
    rightLayout->addLayout(toolRow);

    m_canvasHost = new QWidget(rightContainer);
    m_canvasLayout = new QVBoxLayout(m_canvasHost);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    m_canvas = createCanvas(m_canvasHost);
    m_canvasLayout->addWidget(m_canvas);
    rightLayout->addWidget(m_canvasHost, 1);

    // 色指定(色指定書): キャンバスの下に「肌」「髪 影」などの名前付き色見本を並べる
    rightLayout->addWidget(new QLabel(tr("色指定"), rightContainer));
    m_colorSpecList = new QListWidget(rightContainer);
    m_colorSpecList->setIconSize(QSize(kColorSpecSwatchSize, kColorSpecSwatchSize));
    m_colorSpecList->setFixedHeight(120);
    rightLayout->addWidget(m_colorSpecList);

    auto* colorSpecButtonLayout = new QHBoxLayout();
    auto* addColorSpecButton = new QPushButton(tr("色を追加"), rightContainer);
    auto* renameColorSpecButton = new QPushButton(tr("名前変更"), rightContainer);
    auto* changeColorSpecButton = new QPushButton(tr("色変更"), rightContainer);
    auto* removeColorSpecButton = new QPushButton(tr("削除"), rightContainer);
    colorSpecButtonLayout->addWidget(addColorSpecButton);
    colorSpecButtonLayout->addWidget(renameColorSpecButton);
    colorSpecButtonLayout->addWidget(changeColorSpecButton);
    colorSpecButtonLayout->addWidget(removeColorSpecButton);
    rightLayout->addLayout(colorSpecButtonLayout);

    mainLayout->addWidget(rightContainer, 3);

    setCentralWidget(central);
    m_layerPanel = new LayerPanel(this);
    m_layerPanel->setWindowTitle(tr("設定ボード レイヤー"));
    addDockWidget(Qt::RightDockWidgetArea, m_layerPanel);

    connect(m_penButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        if (m_canvas) m_canvas->setTool(GLCanvas::Tool::Pen);
        m_radiusSlider->setValue(static_cast<int>(m_penRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_penRadius)));
    });
    connect(m_eraserButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        if (m_canvas) m_canvas->setTool(GLCanvas::Tool::Eraser);
        m_radiusSlider->setValue(static_cast<int>(m_eraserRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_eraserRadius)));
    });
    connect(m_fillButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        if (m_canvas) m_canvas->setTool(GLCanvas::Tool::Fill);
    });
    connect(m_eyedropperButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        if (m_canvas) m_canvas->setTool(GLCanvas::Tool::Eyedropper);
    });
    connect(m_radiusSlider, &QSlider::valueChanged, this, &SettingBoardWindow::onRadiusSliderChanged);
    connect(m_colorButton, &QPushButton::clicked, this, &SettingBoardWindow::chooseColor);
    connect(textButton, &QPushButton::clicked, this, &SettingBoardWindow::editTextBoxes);
    connect(pasteButton, &QPushButton::clicked, this, &SettingBoardWindow::pasteImage);
    connect(resizeButton, &QPushButton::clicked, this, &SettingBoardWindow::resizeBoardCanvas);
    connect(exportButton, &QPushButton::clicked, this, &SettingBoardWindow::exportBoardImage);
    connect(m_finalStampButton, &QPushButton::toggled, this, &SettingBoardWindow::toggleFinalStamp);
    connect(detachButton, &QPushButton::clicked, this, &SettingBoardWindow::detachCanvas);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &SettingBoardWindow::onSelectionChanged);
    connect(addButton, &QPushButton::clicked, this, &SettingBoardWindow::addBoard);
    connect(removeButton, &QPushButton::clicked, this, &SettingBoardWindow::removeBoard);
    connect(renameButton, &QPushButton::clicked, this, &SettingBoardWindow::renameBoard);

    connect(m_colorSpecList, &QListWidget::itemDoubleClicked, this, &SettingBoardWindow::onColorSpecActivated);
    connect(addColorSpecButton, &QPushButton::clicked, this, &SettingBoardWindow::addColorSpec);
    connect(renameColorSpecButton, &QPushButton::clicked, this, &SettingBoardWindow::renameColorSpec);
    connect(changeColorSpecButton, &QPushButton::clicked, this, &SettingBoardWindow::changeColorSpecColor);
    connect(removeColorSpecButton, &QPushButton::clicked, this, &SettingBoardWindow::removeColorSpec);

    connect(m_layerPanel, &LayerPanel::layerSelected, this, [this](int layerIndex) {
        if (!m_project) return;
        auto& boards = m_project->settingBoards();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
        core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;
        board.activeLayer = static_cast<size_t>(layerIndex);
        bindCanvasToSelectedBoard();
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::visibilityChanged, this, [this](int layerIndex, bool visible) {
        if (!m_project) return;
        auto& boards = m_project->settingBoards();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
        core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;
        board.layers[static_cast<size_t>(layerIndex)].visible = visible;
        syncSelectedBoardComposite();
        bindCanvasToSelectedBoard();
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::opacityChanged, this, [this](int layerIndex, int opacityPercent) {
        if (!m_project) return;
        auto& boards = m_project->settingBoards();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
        core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;
        board.layers[static_cast<size_t>(layerIndex)].setOpacity(std::clamp(opacityPercent, 0, 100) / 100.0);
        syncSelectedBoardComposite();
        bindCanvasToSelectedBoard();
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::addRequested, this,
            [this] { addPaintLayer(core::LayerRole::Normal); });
    connect(m_layerPanel, &LayerPanel::duplicateRequested, this, &SettingBoardWindow::duplicatePaintLayer);
    connect(m_layerPanel, &LayerPanel::removeRequested, this, &SettingBoardWindow::removePaintLayer);
    connect(m_layerPanel, &LayerPanel::moveUpRequested, this, [this] { movePaintLayer(1); });
    connect(m_layerPanel, &LayerPanel::moveDownRequested, this, [this] { movePaintLayer(-1); });
    connect(m_layerPanel, &LayerPanel::renameRequested, this, &SettingBoardWindow::renamePaintLayer);
    connect(m_layerPanel, &LayerPanel::roleChangeRequested, this, [this](int layerIndex, int role) {
        core::LayerRole layerRole = core::LayerRole::Normal;
        if (role == 1) layerRole = core::LayerRole::ColorTrace;
        if (role == 2) layerRole = core::LayerRole::Correction;
        setPaintLayerRole(layerIndex, layerRole);
    });

    applyToolSettingsToCanvas();

    QToolBar* toolBar = addToolBar(tr("設定ボード"));
    toolBar->setMovable(false);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &SettingBoardWindow::refresh);
}

void SettingBoardWindow::setProject(core::Project* project) {
    m_project = project;
    m_selectedRow = -1;
}

void SettingBoardWindow::refresh() {
    if (!m_list) return;
    m_updating = true;

    if (!m_project) {
        m_list->clear();
        m_selectedRow = -1;
        m_updating = false;
        bindCanvasToSelectedBoard();
        refreshColorSpecList();
        refreshLayerPanel();
        return;
    }

    auto& boards = m_project->settingBoards();
    m_list->clear();
    for (auto& board : boards) {
        syncSettingBoardComposite(board);
        m_list->addItem(QString::fromStdString(board.name));
    }

    if (!boards.empty()) {
        m_selectedRow = std::clamp(m_selectedRow, 0, static_cast<int>(boards.size()) - 1);
        m_list->setCurrentRow(m_selectedRow);  // m_updating中なのでonSelectionChangedは無視される
    } else {
        m_selectedRow = -1;
    }

    m_updating = false;

    // 構造変更後は古いテクスチャを破棄し、vectorの再配置に備えて必ず選択ボードへ再設定する
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    refreshColorSpecList();
    refreshLayerPanel();
}

void SettingBoardWindow::onSelectionChanged() {
    if (m_updating) return;
    const int row = selectedBoardIndex();
    if (row < 0) return;
    m_selectedRow = row;
    bindCanvasToSelectedBoard();
    refreshColorSpecList();
    refreshLayerPanel();
}

int SettingBoardWindow::selectedBoardIndex() const {
    return m_list ? m_list->currentRow() : -1;
}

void SettingBoardWindow::addBoard() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();

    core::SettingBoard board;
    board.image = core::Bitmap(kBoardWidth, kBoardHeight);
    board.image.fill({0, 0, 0, 0});
    perapera::ui::ensurePaintLayers(board.layers, board.activeLayer, board.image, kBoardWidth, kBoardHeight, true);
    board.name = tr("設定ボード %1").arg(boards.size() + 1).toStdString();
    boards.push_back(std::move(board));

    m_selectedRow = static_cast<int>(boards.size()) - 1;
    refresh();
    emit edited();
}

void SettingBoardWindow::removeBoard() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;

    boards.erase(boards.begin() + row);
    m_selectedRow = std::min(row, static_cast<int>(boards.size()) - 1);
    refresh();
    emit edited();
}

void SettingBoardWindow::renameBoard() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(row)];

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("ボード名を変更"), tr("名前:"), QLineEdit::Normal,
                                                    QString::fromStdString(board.name), &ok);
    if (!ok || newName.isEmpty()) return;  // キャンセルまたは空欄は変更しない
    board.name = newName.toStdString();

    m_updating = true;
    if (QListWidgetItem* item = m_list->item(row)) item->setText(newName);
    m_updating = false;

    emit edited();
}

void SettingBoardWindow::onStrokeFinished() {
    syncSelectedBoardComposite();
    emit edited();
}

void SettingBoardWindow::pasteImage() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;

    const QString path = QFileDialog::getOpenFileName(this, tr("画像を貼る"), QString(),
                                                        tr("画像ファイル (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) return;

    const QImage source(path);
    if (source.isNull()) {
        QMessageBox::warning(this, tr("読み込みエラー"), tr("画像を読み込めませんでした"));
        return;
    }

    core::SettingBoard& board = boards[static_cast<size_t>(row)];
    if (board.image.isEmpty()) {
        board.image = core::Bitmap(source.width(), source.height());
        board.image.fill({0, 0, 0, 0});
    }
    perapera::ui::ensurePaintLayers(board.layers, board.activeLayer, board.image, board.image.width(),
                                    board.image.height(), true);
    pasteImageOntoBoard(board.layers[board.activeLayer].bitmap, source);
    syncSettingBoardComposite(board);

    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::resizeBoardCanvas() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;

    core::SettingBoard& board = boards[static_cast<size_t>(row)];
    syncSettingBoardComposite(board);
    const QSize current = boardCanvasSize(board);
    const int currentW = current.width();
    const int currentH = current.height();
    CanvasSizeDialog dialog(currentW, currentH, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int newW = dialog.canvasWidth();
    const int newH = dialog.canvasHeight();
    if (newW == currentW && newH == currentH) return;

    perapera::ui::resizePaintLayersCentered(board.layers, newW, newH);
    moveTextBoxesForCenteredResize(board.textBoxes, currentW, currentH, newW, newH);
    board.image = perapera::ui::compositePaintLayers(board.layers, newW, newH);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::editTextBoxes() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;

    core::SettingBoard& board = boards[static_cast<size_t>(row)];
    syncSettingBoardComposite(board);
    const QSize canvasSize = boardCanvasSize(board);

    const auto makeDefaultBox = [&] {
        core::SettingBoardTextBox box;
        box.x = std::max(0, canvasSize.width() / 16);
        box.y = std::max(0, canvasSize.height() / 16);
        box.width = std::max(1, canvasSize.width() / 3);
        box.height = std::max(1, canvasSize.height() / 7);
        box.fontPixelSize = std::clamp(canvasSize.height() / 24, 24, 72);
        box.color = pixelFromColor(m_penColor);
        return box;
    };

    const auto commitDecorations = [this] {
        updateFinalStampOverlay();
        emit edited();
    };

    if (board.textBoxes.empty()) {
        core::SettingBoardTextBox box = makeDefaultBox();
        if (!editSettingBoardTextBox(this, canvasSize, &box)) return;
        board.textBoxes.push_back(std::move(box));
        commitDecorations();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("文字ボックス"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);

    const auto refreshList = [&] {
        list->clear();
        for (int i = 0; i < static_cast<int>(board.textBoxes.size()); ++i) {
            list->addItem(textBoxPreview(board.textBoxes[static_cast<size_t>(i)], i));
        }
        if (list->count() > 0 && list->currentRow() < 0) list->setCurrentRow(0);
    };
    refreshList();

    auto* buttonRow = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("追加"), &dialog);
    auto* editButton = new QPushButton(tr("編集"), &dialog);
    auto* removeButton = new QPushButton(tr("削除"), &dialog);
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(editButton);
    buttonRow->addWidget(removeButton);
    layout->addLayout(buttonRow);

    const auto editCurrent = [&] {
        const int selected = list->currentRow();
        if (selected < 0 || static_cast<size_t>(selected) >= board.textBoxes.size()) return;
        if (!editSettingBoardTextBox(&dialog, canvasSize, &board.textBoxes[static_cast<size_t>(selected)])) return;
        refreshList();
        list->setCurrentRow(selected);
        commitDecorations();
    };

    connect(addButton, &QPushButton::clicked, &dialog, [&] {
        core::SettingBoardTextBox box = makeDefaultBox();
        if (!editSettingBoardTextBox(&dialog, canvasSize, &box)) return;
        board.textBoxes.push_back(std::move(box));
        refreshList();
        list->setCurrentRow(static_cast<int>(board.textBoxes.size()) - 1);
        commitDecorations();
    });
    connect(editButton, &QPushButton::clicked, &dialog, editCurrent);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem*) { editCurrent(); });
    connect(removeButton, &QPushButton::clicked, &dialog, [&] {
        const int selected = list->currentRow();
        if (selected < 0 || static_cast<size_t>(selected) >= board.textBoxes.size()) return;
        board.textBoxes.erase(board.textBoxes.begin() + selected);
        refreshList();
        if (list->count() > 0) list->setCurrentRow(std::min(selected, list->count() - 1));
        commitDecorations();
    });

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(closeButtons);
    dialog.exec();
}

void SettingBoardWindow::toggleFinalStamp(bool checked) {
    if (m_updating || !m_project) return;
    auto& boards = m_project->settingBoards();
    const int row = selectedBoardIndex();
    if (row < 0 || static_cast<size_t>(row) >= boards.size()) return;

    boards[static_cast<size_t>(row)].finalStamp = checked;
    updateFinalStampOverlay();
    emit edited();
}

void SettingBoardWindow::updateFinalStampOverlay() {
    if (!m_project || m_selectedRow < 0) {
        m_canvas->clearOverlay();
        return;
    }
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) {
        m_canvas->clearOverlay();
        return;
    }
    const core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    const QSize size = boardCanvasSize(board);
    if (hasBoardDecorations(board) && size.width() > 0 && size.height() > 0) {
        m_canvas->setOverlayImage(makeBoardDecorationsOverlay(board, size.width(), size.height()));
    } else {
        m_canvas->clearOverlay();
    }
}

void SettingBoardWindow::detachCanvas() {
    if (m_floatingCanvasWindow || !m_canvas || !m_canvasLayout) return;
    m_canvasLayout->removeWidget(m_canvas);
    m_canvas->deleteLater();
    m_canvas = nullptr;
    auto* window = new FloatingCanvasWindow(tr("設定ボード キャンバス"), this);
    m_floatingCanvasWindow = window;
    perapera::ui::installRetroWindowFrame(window);
    window->setCentralWidget(createFloatingCanvasPanel(window));
    bindCanvasToSelectedBoard();
    connect(window, &FloatingCanvasWindow::restoreRequested, this, &SettingBoardWindow::restoreCanvas);
    connect(window, &QObject::destroyed, this, [this, window] {
        if (m_floatingCanvasWindow == window) m_floatingCanvasWindow = nullptr;
    });
    window->show();
    perapera::ui::keepWindowOnScreen(window);
}

void SettingBoardWindow::restoreCanvas() {
    if (!m_floatingCanvasWindow || !m_canvasLayout) return;
    FloatingCanvasWindow* window = m_floatingCanvasWindow;
    if (QWidget* floatingCentral = window->centralWidget()) {
        if (auto* oldCanvas = floatingCentral->findChild<GLCanvas*>()) oldCanvas->shutdownForClose();
        window->takeCentralWidget();
        floatingCentral->deleteLater();
    }
    m_canvas = nullptr;
    m_canvas = createCanvas(m_canvasHost);
    m_canvasLayout->addWidget(m_canvas);
    bindCanvasToSelectedBoard();
    m_canvas->show();
    m_floatingCanvasWindow = nullptr;
}

void SettingBoardWindow::debugDetachCanvas() {
    detachCanvas();
}

FloatingCanvasWindow* SettingBoardWindow::debugFloatingCanvasWindow() const {
    return m_floatingCanvasWindow;
}

bool SettingBoardWindow::debugExportSelectedBoardImage(const QString& path) {
    if (!m_project || m_selectedRow < 0) return false;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return false;
    const QString pngPath = path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? path : path + ".png";
    const QImage image = boardExportImage(boards[static_cast<size_t>(m_selectedRow)]);
    return !image.isNull() && image.save(pngPath);
}

void SettingBoardWindow::bindCanvasToSelectedBoard() {
    if (!m_project) {
        m_canvas->setBitmap(nullptr);
        m_canvas->setFillBoundaryLayers({});
        m_canvas->clearOverlay();
        if (m_finalStampButton) {
            const QSignalBlocker blocker(m_finalStampButton);
            m_finalStampButton->setChecked(false);
            m_finalStampButton->setEnabled(false);
        }
        refreshLayerPanel();
        return;
    }
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) {
        m_canvas->setBitmap(nullptr);
        m_canvas->setFillBoundaryLayers({});
        m_canvas->clearOverlay();
        if (m_finalStampButton) {
            const QSignalBlocker blocker(m_finalStampButton);
            m_finalStampButton->setChecked(false);
            m_finalStampButton->setEnabled(false);
        }
        refreshLayerPanel();
        return;
    }

    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    // imageはボード全体(kBoardWidth x kBoardHeight)を覆う1枚のビットマップ。
    // 未確保またはサイズ不一致なら確保し直す(開発中につき旧サイズのデータは破棄してよい)
    syncSettingBoardComposite(board);
    const QSize size = boardCanvasSize(board);
    m_canvas->setCanvasSize(size.width(), size.height());

    std::vector<GLCanvas::StackEntry> stack;
    std::vector<const core::Bitmap*> boundary;
    for (const core::PaintLayer& layer : board.layers) {
        if (layer.visible && layer.opacity > 0.0) {
            stack.push_back({&layer.bitmap, QPointF(), layer.opacity});
            if (layer.role == core::LayerRole::ColorTrace) boundary.push_back(&layer.bitmap);
        }
    }
    core::Bitmap* editTarget = board.layers.empty() ? nullptr : &board.layers[board.activeLayer].bitmap;
    m_canvas->setFillBoundaryLayers(std::move(boundary));
    m_canvas->setLayerStack(std::move(stack), editTarget, QPointF());
    if (m_finalStampButton) {
        const QSignalBlocker blocker(m_finalStampButton);
        m_finalStampButton->setEnabled(true);
        m_finalStampButton->setChecked(board.finalStamp);
    }
    updateFinalStampOverlay();
    refreshLayerPanel();
}

void SettingBoardWindow::onRadiusSliderChanged(int value) {
    if (m_eraserButton->isChecked()) {
        m_eraserRadius = static_cast<float>(value);
    } else {
        m_penRadius = static_cast<float>(value);
    }
    m_radiusValueLabel->setText(QString::number(value));
    applyToolSettingsToCanvas();
}

void SettingBoardWindow::chooseColor() {
    const QColor chosen = QColorDialog::getColor(m_penColor, this, tr("ペンの色"));
    if (!chosen.isValid()) return;
    m_penColor = chosen;
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    applyToolSettingsToCanvas();
}

void SettingBoardWindow::applyToolSettingsToCanvas() {
    if (!m_canvas) return;
    m_canvas->setPenRadius(m_penRadius);
    m_canvas->setPenColor(m_penColor);
    m_canvas->setEraserRadius(m_eraserRadius);
}

GLCanvas* SettingBoardWindow::createCanvas(QWidget* parent) {
    auto* canvas = new GLCanvas(parent);
    canvas->setCanvasSize(kBoardWidth, kBoardHeight);
    if (m_eyedropperButton && m_eyedropperButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Eyedropper);
    } else if (m_fillButton && m_fillButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Fill);
    } else if (m_eraserButton && m_eraserButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Eraser);
    } else {
        canvas->setTool(GLCanvas::Tool::Pen);
    }
    canvas->setPenRadius(m_penRadius);
    canvas->setPenColor(m_penColor);
    canvas->setEraserRadius(m_eraserRadius);
    canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command>) { onStrokeFinished(); });
    connect(canvas, &GLCanvas::colorPicked, this, [this](QColor color) {
        m_penColor = color;
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
        applyToolSettingsToCanvas();
        m_penButton->setChecked(true);
    });
    return canvas;
}

QWidget* SettingBoardWindow::createFloatingCanvasPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* row = new QHBoxLayout();
    auto* penButton = new QPushButton(tr("ペン"), panel);
    auto* eraserButton = new QPushButton(tr("消しゴム"), panel);
    auto* fillButton = new QPushButton(tr("塗りつぶし"), panel);
    auto* eyedropperButton = new QPushButton(tr("スポイト"), panel);
    for (QPushButton* button : {penButton, eraserButton, fillButton, eyedropperButton}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        row->addWidget(button);
    }
    if (m_eyedropperButton && m_eyedropperButton->isChecked()) {
        eyedropperButton->setChecked(true);
    } else if (m_fillButton && m_fillButton->isChecked()) {
        fillButton->setChecked(true);
    } else if (m_eraserButton && m_eraserButton->isChecked()) {
        eraserButton->setChecked(true);
    } else {
        penButton->setChecked(true);
    }

    row->addWidget(new QLabel(tr("太さ"), panel));
    auto* radiusSlider = new QSlider(Qt::Horizontal, panel);
    radiusSlider->setRange(kRadiusMin, kRadiusMax);
    radiusSlider->setValue(m_eraserButton && m_eraserButton->isChecked() ? static_cast<int>(m_eraserRadius)
                                                                         : static_cast<int>(m_penRadius));
    radiusSlider->setFixedWidth(140);
    row->addWidget(radiusSlider);
    auto* radiusLabel = new QLabel(QString::number(radiusSlider->value()), panel);
    radiusLabel->setFixedWidth(28);
    row->addWidget(radiusLabel);

    auto* colorButton = new QPushButton(tr("色"), panel);
    colorButton->setFixedWidth(48);
    colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    row->addWidget(colorButton);
    auto* textButton = new QPushButton(tr("文字"), panel);
    row->addWidget(textButton);
    row->addStretch();
    layout->addLayout(row);

    m_canvas = createCanvas(panel);
    layout->addWidget(m_canvas, 1);

    connect(penButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) setActiveTool(static_cast<int>(GLCanvas::Tool::Pen));
    });
    connect(eraserButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) setActiveTool(static_cast<int>(GLCanvas::Tool::Eraser));
    });
    connect(fillButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) setActiveTool(static_cast<int>(GLCanvas::Tool::Fill));
    });
    connect(eyedropperButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) setActiveTool(static_cast<int>(GLCanvas::Tool::Eyedropper));
    });
    connect(radiusSlider, &QSlider::valueChanged, this, [this, radiusLabel](int value) {
        if (m_eraserButton && m_eraserButton->isChecked()) {
            m_eraserRadius = static_cast<float>(value);
        } else {
            m_penRadius = static_cast<float>(value);
        }
        radiusLabel->setText(QString::number(value));
        if (m_radiusSlider) {
            const QSignalBlocker blocker(m_radiusSlider);
            m_radiusSlider->setValue(value);
        }
        if (m_radiusValueLabel) m_radiusValueLabel->setText(QString::number(value));
        applyToolSettingsToCanvas();
    });
    connect(colorButton, &QPushButton::clicked, this, [this, colorButton] {
        const QColor chosen = QColorDialog::getColor(m_penColor, this, tr("ペンの色"));
        if (!chosen.isValid()) return;
        m_penColor = chosen;
        const QString style = QStringLiteral("background-color: %1;").arg(m_penColor.name());
        colorButton->setStyleSheet(style);
        if (m_colorButton) m_colorButton->setStyleSheet(style);
        applyToolSettingsToCanvas();
    });
    connect(textButton, &QPushButton::clicked, this, &SettingBoardWindow::editTextBoxes);
    return panel;
}

void SettingBoardWindow::setActiveTool(int tool) {
    const auto canvasTool = static_cast<GLCanvas::Tool>(tool);
    if (m_penButton && m_eraserButton && m_fillButton && m_eyedropperButton) {
        const QSignalBlocker b1(m_penButton);
        const QSignalBlocker b2(m_eraserButton);
        const QSignalBlocker b3(m_fillButton);
        const QSignalBlocker b4(m_eyedropperButton);
        m_penButton->setChecked(canvasTool == GLCanvas::Tool::Pen);
        m_eraserButton->setChecked(canvasTool == GLCanvas::Tool::Eraser);
        m_fillButton->setChecked(canvasTool == GLCanvas::Tool::Fill);
        m_eyedropperButton->setChecked(canvasTool == GLCanvas::Tool::Eyedropper);
    }
    if (m_canvas) m_canvas->setTool(canvasTool);
    if (m_radiusSlider && m_radiusValueLabel) {
        const int value = canvasTool == GLCanvas::Tool::Eraser ? static_cast<int>(m_eraserRadius)
                                                               : static_cast<int>(m_penRadius);
        const QSignalBlocker blocker(m_radiusSlider);
        m_radiusSlider->setValue(value);
        m_radiusValueLabel->setText(QString::number(value));
    }
}

void SettingBoardWindow::syncSelectedBoardComposite() {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    syncSettingBoardComposite(boards[static_cast<size_t>(m_selectedRow)]);
}

void SettingBoardWindow::refreshLayerPanel() {
    if (!m_layerPanel) return;
    QStringList names;
    QList<bool> visible;
    QList<int> opacity;
    int active = -1;

    if (m_project && m_selectedRow >= 0) {
        auto& boards = m_project->settingBoards();
        if (static_cast<size_t>(m_selectedRow) < boards.size()) {
            core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
            perapera::ui::ensurePaintLayers(board.layers, board.activeLayer, board.image, kBoardWidth, kBoardHeight,
                                            true);
            active = static_cast<int>(board.activeLayer);
            for (const core::PaintLayer& layer : board.layers) {
                QString name = QString::fromStdString(layer.name);
                if (layer.role == core::LayerRole::ColorTrace) name += tr(" [塗分け線]");
                if (layer.role == core::LayerRole::Correction) name += tr(" [修正]");
                names.append(name);
                visible.append(layer.visible);
                opacity.append(static_cast<int>(std::lround(std::clamp(layer.opacity, 0.0, 1.0) * 100.0)));
            }
        }
    }

    m_layerPanel->setLayers(names, visible, opacity, active);
}

void SettingBoardWindow::addPaintLayer(core::LayerRole role) {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    syncSettingBoardComposite(board);
    const QSize size = boardCanvasSize(board);

    core::PaintLayer layer;
    layer.role = role;
    layer.name = role == core::LayerRole::ColorTrace ? "塗分け線" : "レイヤー";
    layer.bitmap = core::Bitmap(size.width(), size.height());
    layer.bitmap.fill({0, 0, 0, 0});
    const size_t insertAt = std::min(board.activeLayer + 1, board.layers.size());
    board.layers.insert(board.layers.begin() + static_cast<ptrdiff_t>(insertAt), std::move(layer));
    board.activeLayer = insertAt;
    syncSettingBoardComposite(board);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::duplicatePaintLayer(int layerIndex) {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;
    core::PaintLayer copy = board.layers[static_cast<size_t>(layerIndex)];
    copy.name += " コピー";
    board.layers.insert(board.layers.begin() + layerIndex + 1, std::move(copy));
    board.activeLayer = static_cast<size_t>(layerIndex + 1);
    syncSettingBoardComposite(board);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::removePaintLayer() {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    if (board.layers.size() <= 1) return;
    board.layers.erase(board.layers.begin() + static_cast<ptrdiff_t>(board.activeLayer));
    board.activeLayer = std::min(board.activeLayer, board.layers.size() - 1);
    syncSettingBoardComposite(board);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::movePaintLayer(int delta) {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    const int from = static_cast<int>(board.activeLayer);
    const int to = from + delta;
    if (from < 0 || to < 0 || static_cast<size_t>(to) >= board.layers.size()) return;
    std::swap(board.layers[static_cast<size_t>(from)], board.layers[static_cast<size_t>(to)]);
    board.activeLayer = static_cast<size_t>(to);
    syncSettingBoardComposite(board);
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::renamePaintLayer(int layerIndex) {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("レイヤー名"), tr("名前:"), QLineEdit::Normal,
                                               QString::fromStdString(board.layers[static_cast<size_t>(layerIndex)].name),
                                               &ok);
    if (!ok || name.isEmpty()) return;
    board.layers[static_cast<size_t>(layerIndex)].name = name.toStdString();
    refreshLayerPanel();
    emit edited();
}

void SettingBoardWindow::setPaintLayerRole(int layerIndex, core::LayerRole role) {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= board.layers.size()) return;
    board.layers[static_cast<size_t>(layerIndex)].role = role;
    bindCanvasToSelectedBoard();
    emit edited();
}

void SettingBoardWindow::exportBoardImage() {
    if (!m_project || m_selectedRow < 0) return;
    auto& boards = m_project->settingBoards();
    if (static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];

    const QString path =
        QFileDialog::getSaveFileName(this, tr("設定ボードを書き出し"), QString(), tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    const QString pngPath = path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? path : path + ".png";
    const QImage image = boardExportImage(board);
    if (image.isNull() || !image.save(pngPath)) {
        QMessageBox::warning(this, tr("書き出しエラー"), tr("PNGを書き出せませんでした"));
    }
}

int SettingBoardWindow::selectedColorSpecIndex() const {
    return m_colorSpecList ? m_colorSpecList->currentRow() : -1;
}

// 選択中ボードのcolorSpecsから色指定リストを再構築する。ボード未選択時は空にする
void SettingBoardWindow::refreshColorSpecList() {
    if (!m_colorSpecList) return;
    m_updating = true;
    m_colorSpecList->clear();

    if (m_project && m_selectedRow >= 0) {
        auto& boards = m_project->settingBoards();
        if (static_cast<size_t>(m_selectedRow) < boards.size()) {
            for (const core::ColorSpec& spec : boards[static_cast<size_t>(m_selectedRow)].colorSpecs) {
                QPixmap pixmap(kColorSpecSwatchSize, kColorSpecSwatchSize);
                pixmap.fill(QColor(spec.color.r, spec.color.g, spec.color.b, spec.color.a));
                auto* item = new QListWidgetItem(QIcon(pixmap), QString::fromStdString(spec.name));
                m_colorSpecList->addItem(item);
            }
        }
    }

    m_updating = false;
}

// 色を選び、名前を付けて選択中ボードのcolorSpecsへ追加する
void SettingBoardWindow::addColorSpec() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;

    const QColor chosen = QColorDialog::getColor(Qt::white, this, tr("色指定の色"));
    if (!chosen.isValid()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("色指定の名前"), tr("名前(例: 肌、髪 影):"),
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    core::ColorSpec spec;
    spec.name = name.toStdString();
    spec.color = {static_cast<uint8_t>(chosen.red()), static_cast<uint8_t>(chosen.green()),
                  static_cast<uint8_t>(chosen.blue()), static_cast<uint8_t>(chosen.alpha())};
    boards[static_cast<size_t>(m_selectedRow)].colorSpecs.push_back(spec);

    refreshColorSpecList();
    emit edited();
}

// 選択中の色指定の名前を変更する
void SettingBoardWindow::renameColorSpec() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    auto& specs = boards[static_cast<size_t>(m_selectedRow)].colorSpecs;
    const int row = selectedColorSpecIndex();
    if (row < 0 || static_cast<size_t>(row) >= specs.size()) return;

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, tr("名前を変更"), tr("名前:"), QLineEdit::Normal,
                               QString::fromStdString(specs[static_cast<size_t>(row)].name), &ok);
    if (!ok || newName.isEmpty()) return;
    specs[static_cast<size_t>(row)].name = newName.toStdString();

    refreshColorSpecList();
    m_colorSpecList->setCurrentRow(row);
    emit edited();
}

// 選択中の色指定の色を変更する
void SettingBoardWindow::changeColorSpecColor() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    auto& specs = boards[static_cast<size_t>(m_selectedRow)].colorSpecs;
    const int row = selectedColorSpecIndex();
    if (row < 0 || static_cast<size_t>(row) >= specs.size()) return;

    const core::Bitmap::Pixel& current = specs[static_cast<size_t>(row)].color;
    const QColor chosen =
        QColorDialog::getColor(QColor(current.r, current.g, current.b, current.a), this, tr("色指定の色"));
    if (!chosen.isValid()) return;
    specs[static_cast<size_t>(row)].color = {static_cast<uint8_t>(chosen.red()), static_cast<uint8_t>(chosen.green()),
                                              static_cast<uint8_t>(chosen.blue()),
                                              static_cast<uint8_t>(chosen.alpha())};

    refreshColorSpecList();
    m_colorSpecList->setCurrentRow(row);
    emit edited();
}

// 選択中の色指定を削除する
void SettingBoardWindow::removeColorSpec() {
    if (!m_project) return;
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    auto& specs = boards[static_cast<size_t>(m_selectedRow)].colorSpecs;
    const int row = selectedColorSpecIndex();
    if (row < 0 || static_cast<size_t>(row) >= specs.size()) return;

    specs.erase(specs.begin() + row);

    refreshColorSpecList();
    if (!specs.empty()) {
        m_colorSpecList->setCurrentRow(std::min(row, static_cast<int>(specs.size()) - 1));
    }
    emit edited();
}

// 色指定リストの行をダブルクリック: その色を現在のペン色に設定し、全キャンバス(このウィンドウの
// 描画エリア)へ適用、色ボタンのスウォッチも更新する
void SettingBoardWindow::onColorSpecActivated(QListWidgetItem* item) {
    if (!item || !m_project) return;
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) return;
    const auto& specs = boards[static_cast<size_t>(m_selectedRow)].colorSpecs;
    const int row = m_colorSpecList->row(item);
    if (row < 0 || static_cast<size_t>(row) >= specs.size()) return;

    const core::Bitmap::Pixel& color = specs[static_cast<size_t>(row)].color;
    m_penColor = QColor(color.r, color.g, color.b, color.a);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    applyToolSettingsToCanvas();
}
