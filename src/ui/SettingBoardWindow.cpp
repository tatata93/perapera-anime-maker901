#include "SettingBoardWindow.h"

#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QFileDialog>
#include <QFont>
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
#include <QPushButton>
#include <QSize>
#include <QSlider>
#include <QSignalBlocker>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Project.h"
#include "render/GLCanvas.h"
#include "ui/CanvasSizeDialog.h"
#include "ui/FloatingCanvasWindow.h"
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
    m_eyedropperButton->setCheckable(true);
    m_penButton->setAutoExclusive(true);
    m_eraserButton->setAutoExclusive(true);
    m_eyedropperButton->setAutoExclusive(true);
    m_penButton->setChecked(true);
    toolRow->addWidget(m_penButton);
    toolRow->addWidget(m_eraserButton);
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

    auto* pasteButton = new QPushButton(tr("画像を貼る"), rightContainer);
    toolRow->addWidget(pasteButton);
    auto* resizeButton = new QPushButton(tr("サイズ"), rightContainer);
    toolRow->addWidget(resizeButton);
    m_finalStampButton = new QPushButton(tr("決定稿"), rightContainer);
    m_finalStampButton->setCheckable(true);
    toolRow->addWidget(m_finalStampButton);
    auto* detachButton = new QPushButton(tr("別窓"), rightContainer);
    toolRow->addWidget(detachButton);

    toolRow->addStretch();
    rightLayout->addLayout(toolRow);

    m_canvas = new GLCanvas(rightContainer);
    m_canvas->setCanvasSize(kBoardWidth, kBoardHeight);
    m_canvas->setTool(GLCanvas::Tool::Pen);
    m_canvasHost = new QWidget(rightContainer);
    m_canvasLayout = new QVBoxLayout(m_canvasHost);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
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

    m_canvas->setStrokeCommandSink(
        [this](std::unique_ptr<core::Command>) { onStrokeFinished(); });

    connect(m_penButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        m_canvas->setTool(GLCanvas::Tool::Pen);
        m_radiusSlider->setValue(static_cast<int>(m_penRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_penRadius)));
    });
    connect(m_eraserButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        m_canvas->setTool(GLCanvas::Tool::Eraser);
        m_radiusSlider->setValue(static_cast<int>(m_eraserRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_eraserRadius)));
    });
    connect(m_eyedropperButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        m_canvas->setTool(GLCanvas::Tool::Eyedropper);
    });
    connect(m_canvas, &GLCanvas::colorPicked, this, [this](QColor color) {
        m_penColor = color;
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
        applyToolSettingsToCanvas();
        m_penButton->setChecked(true);
    });
    connect(m_radiusSlider, &QSlider::valueChanged, this, &SettingBoardWindow::onRadiusSliderChanged);
    connect(m_colorButton, &QPushButton::clicked, this, &SettingBoardWindow::chooseColor);
    connect(pasteButton, &QPushButton::clicked, this, &SettingBoardWindow::pasteImage);
    connect(resizeButton, &QPushButton::clicked, this, &SettingBoardWindow::resizeBoardCanvas);
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
        return;
    }

    auto& boards = m_project->settingBoards();
    m_list->clear();
    for (const auto& board : boards) {
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
}

void SettingBoardWindow::onSelectionChanged() {
    if (m_updating) return;
    const int row = selectedBoardIndex();
    if (row < 0) return;
    m_selectedRow = row;
    bindCanvasToSelectedBoard();
    refreshColorSpecList();
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
    pasteImageOntoBoard(board.image, source);

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
    const int currentW = board.image.isEmpty() ? kBoardWidth : board.image.width();
    const int currentH = board.image.isEmpty() ? kBoardHeight : board.image.height();
    CanvasSizeDialog dialog(currentW, currentH, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const int newW = dialog.canvasWidth();
    const int newH = dialog.canvasHeight();
    if (newW == currentW && newH == currentH) return;

    resizeBitmapCentered(board.image, newW, newH);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedBoard();
    emit edited();
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
    if (board.finalStamp && !board.image.isEmpty()) {
        m_canvas->setOverlayImage(makeFinalStampOverlay(board.image.width(), board.image.height()));
    } else {
        m_canvas->clearOverlay();
    }
}

void SettingBoardWindow::detachCanvas() {
    if (m_floatingCanvasWindow || !m_canvas || !m_canvasLayout) return;
    m_canvasLayout->removeWidget(m_canvas);
    auto* window = new FloatingCanvasWindow(tr("設定ボード キャンバス"), this);
    m_floatingCanvasWindow = window;
    perapera::ui::installRetroWindowFrame(window);
    window->setCentralWidget(m_canvas);
    connect(window, &FloatingCanvasWindow::restoreRequested, this, &SettingBoardWindow::restoreCanvas);
    connect(window, &QObject::destroyed, this, [this] { m_floatingCanvasWindow = nullptr; });
    window->show();
}

void SettingBoardWindow::restoreCanvas() {
    if (!m_floatingCanvasWindow || !m_canvasLayout) return;
    FloatingCanvasWindow* window = m_floatingCanvasWindow;
    QWidget* canvas = window->takeCentralWidget();
    if (canvas) {
        m_canvasLayout->addWidget(canvas);
        canvas->show();
    }
    m_floatingCanvasWindow = nullptr;
    window->deleteLater();
}

void SettingBoardWindow::bindCanvasToSelectedBoard() {
    if (!m_project) {
        m_canvas->setBitmap(nullptr);
        m_canvas->clearOverlay();
        if (m_finalStampButton) {
            const QSignalBlocker blocker(m_finalStampButton);
            m_finalStampButton->setChecked(false);
            m_finalStampButton->setEnabled(false);
        }
        return;
    }
    auto& boards = m_project->settingBoards();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= boards.size()) {
        m_canvas->setBitmap(nullptr);
        m_canvas->clearOverlay();
        if (m_finalStampButton) {
            const QSignalBlocker blocker(m_finalStampButton);
            m_finalStampButton->setChecked(false);
            m_finalStampButton->setEnabled(false);
        }
        return;
    }

    core::SettingBoard& board = boards[static_cast<size_t>(m_selectedRow)];
    // imageはボード全体(kBoardWidth x kBoardHeight)を覆う1枚のビットマップ。
    // 未確保またはサイズ不一致なら確保し直す(開発中につき旧サイズのデータは破棄してよい)
    if (board.image.isEmpty()) {
        board.image = core::Bitmap(kBoardWidth, kBoardHeight);
        board.image.fill({0, 0, 0, 0});
    }
    m_canvas->setCanvasSize(board.image.width(), board.image.height());
    m_canvas->setBitmap(&board.image);
    if (m_finalStampButton) {
        const QSignalBlocker blocker(m_finalStampButton);
        m_finalStampButton->setEnabled(true);
        m_finalStampButton->setChecked(board.finalStamp);
    }
    updateFinalStampOverlay();
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
    m_canvas->setPenRadius(m_penRadius);
    m_canvas->setPenColor(m_penColor);
    m_canvas->setEraserRadius(m_eraserRadius);
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
