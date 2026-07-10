#include "StoryboardWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTextOption>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

#include "core/Project.h"
#include "render/GLCanvas.h"

namespace {
// 絵コンテのパネルは全カット共通で960x540の紙に描く(実際の作画キャンバスとは独立)
constexpr int kPanelWidth = 960;
constexpr int kPanelHeight = 540;
// 内容欄/セリフ欄の紙サイズ(ジブリ/東映系コンテ用紙の縦長の欄を模す)
constexpr int kMemoWidth = 400;
constexpr int kMemoHeight = 540;
// サムネイル解像度(表示は列幅に合わせて縮小する)
constexpr int kThumbWidth = 96;
constexpr int kThumbHeight = 54;
constexpr int kRowHeight = 70;
constexpr double kFps = 24.0;  // タイムシートは24fps基準

// 太さスライダーの範囲(ペン/消しゴム共通)
constexpr int kRadiusMin = 1;
constexpr int kRadiusMax = 40;

enum Column {
    kColNo = 0,
    kColThumb,
    kColCutLabel,
    kColAction,
    kColDialogue,
    kColDuration,
    kColCount,
};

// drawing(透明ビットマップ)を白背景に合成してからサムネイルサイズへ縮小する。
// 透明のまま縮小するとデコレーション表示上は黒く見えてしまうため
QPixmap makeThumbnail(const core::Bitmap& bitmap) {
    if (bitmap.isEmpty()) return QPixmap();
    const QImage source(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    QImage composed(bitmap.width(), bitmap.height(), QImage::Format_RGB32);
    composed.fill(Qt::white);
    QPainter painter(&composed);
    painter.drawImage(0, 0, source);
    painter.end();
    // drawImage()は即座にピクセルをコピーするためsource/bitmap破棄後も安全
    return QPixmap::fromImage(
        composed.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// 内容欄/セリフ欄のテキストをQImageに描画する(GLCanvasの下敷きとして敷き、手書きインクと重ねるため)。
// 白地に黒文字で塗るため、乗算合成の下敷きとしてインク(通常は不透明な線)と共存できる
QImage renderTextUnderlay(const QString& text) {
    QImage image(kMemoWidth, kMemoHeight, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPixelSize(22);
    painter.setFont(font);

    constexpr int kMargin = 12;
    const QRect textRect(kMargin, kMargin, kMemoWidth - kMargin * 2, kMemoHeight - kMargin * 2);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    painter.drawText(textRect, text, option);
    painter.end();
    return image;
}

}  // namespace

StoryboardWindow::StoryboardWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("絵コンテ - perapera-anime-maker901"));
    resize(1200, 700);

    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);

    // 左: パネル表+ボタン列
    auto* leftContainer = new QWidget(central);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(leftContainer);
    m_table->setColumnCount(kColCount);
    m_table->setHorizontalHeaderLabels(
        {tr("No"), tr("絵"), tr("カット番号"), tr("内容"), tr("セリフ"), tr("尺コマ")});
    m_table->verticalHeader()->setVisible(false);  // No列で番号を表示するため行ヘッダは隠す
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_table->setColumnWidth(kColThumb, kThumbWidth + 16);
    m_table->setColumnWidth(kColCutLabel, 80);
    m_table->setColumnWidth(kColAction, 200);
    m_table->setColumnWidth(kColDialogue, 200);
    m_table->setColumnWidth(kColDuration, 80);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_table);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("パネル追加"), leftContainer);
    auto* removeButton = new QPushButton(tr("パネル削除"), leftContainer);
    auto* upButton = new QPushButton(tr("上へ"), leftContainer);
    auto* downButton = new QPushButton(tr("下へ"), leftContainer);
    auto* createCutButton = new QPushButton(tr("パネルからカット作成"), leftContainer);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addWidget(upButton);
    buttonLayout->addWidget(downButton);
    buttonLayout->addWidget(createCutButton);
    leftLayout->addLayout(buttonLayout);
    mainLayout->addWidget(leftContainer, 3);

    // 右: ツール行+描画エリア(GLCanvasを絵コンテ専用の紙として再利用する)
    auto* rightContainer = new QWidget(central);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto* toolRow = new QHBoxLayout();
    m_penButton = new QPushButton(tr("ペン"), rightContainer);
    m_eraserButton = new QPushButton(tr("消しゴム"), rightContainer);
    m_penButton->setCheckable(true);
    m_eraserButton->setCheckable(true);
    m_penButton->setAutoExclusive(true);
    m_eraserButton->setAutoExclusive(true);
    m_penButton->setChecked(true);
    toolRow->addWidget(m_penButton);
    toolRow->addWidget(m_eraserButton);

    // 太さ(選択中ツールの半径。ペン/消しゴムそれぞれの値をメンバで記憶し、トグル切替時に表示も切替)
    toolRow->addWidget(new QLabel(tr("太さ"), rightContainer));
    m_radiusSlider = new QSlider(Qt::Horizontal, rightContainer);
    m_radiusSlider->setRange(kRadiusMin, kRadiusMax);
    m_radiusSlider->setValue(static_cast<int>(m_penRadius));
    m_radiusSlider->setFixedWidth(120);
    toolRow->addWidget(m_radiusSlider);
    m_radiusValueLabel = new QLabel(QString::number(static_cast<int>(m_penRadius)), rightContainer);
    m_radiusValueLabel->setFixedWidth(24);
    toolRow->addWidget(m_radiusValueLabel);

    // 色(クリックでQColorDialogを開き、選択色をボタン背景にも反映する)
    m_colorButton = new QPushButton(tr("色"), rightContainer);
    m_colorButton->setFixedWidth(48);
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    toolRow->addWidget(m_colorButton);

    toolRow->addStretch();
    rightLayout->addLayout(toolRow);

    // コンテ用紙風の3欄(ジブリ/東映系コンテ用紙を参考に、絵の右に内容欄/セリフ欄を縦長に並べる)。
    // 内容欄/セリフ欄はテキスト入力の上に手書きで重ね書きできるようGLCanvasを重ねる
    auto* paperLayout = new QHBoxLayout();

    m_canvas = new GLCanvas(rightContainer);
    m_canvas->setCanvasSize(kPanelWidth, kPanelHeight);
    m_canvas->setTool(GLCanvas::Tool::Pen);
    paperLayout->addWidget(m_canvas, 12);

    auto* actionColumn = new QVBoxLayout();
    auto* actionHeader = new QLabel(tr("内容"), rightContainer);
    actionHeader->setAlignment(Qt::AlignCenter);
    actionColumn->addWidget(actionHeader);
    m_actionCanvas = new GLCanvas(rightContainer);
    m_actionCanvas->setCanvasSize(kMemoWidth, kMemoHeight);
    m_actionCanvas->setTool(GLCanvas::Tool::Pen);
    actionColumn->addWidget(m_actionCanvas);
    paperLayout->addLayout(actionColumn, 5);

    auto* dialogueColumn = new QVBoxLayout();
    auto* dialogueHeader = new QLabel(tr("セリフ"), rightContainer);
    dialogueHeader->setAlignment(Qt::AlignCenter);
    dialogueColumn->addWidget(dialogueHeader);
    m_dialogueCanvas = new GLCanvas(rightContainer);
    m_dialogueCanvas->setCanvasSize(kMemoWidth, kMemoHeight);
    m_dialogueCanvas->setTool(GLCanvas::Tool::Pen);
    dialogueColumn->addWidget(m_dialogueCanvas);
    paperLayout->addLayout(dialogueColumn, 5);

    rightLayout->addLayout(paperLayout, 2);

    // 内容/セリフ(複数行対応のテキスト欄。表の該当列は表示専用にする)。上の手書き欄と横並びにする
    auto* textRow = new QHBoxLayout();

    auto* actionTextColumn = new QVBoxLayout();
    actionTextColumn->addWidget(new QLabel(tr("内容(テキスト)"), rightContainer));
    m_actionEdit = new QPlainTextEdit(rightContainer);
    m_actionEdit->setFixedHeight(60);
    m_actionEdit->setEnabled(false);
    actionTextColumn->addWidget(m_actionEdit);
    textRow->addLayout(actionTextColumn);

    auto* dialogueTextColumn = new QVBoxLayout();
    dialogueTextColumn->addWidget(new QLabel(tr("セリフ(テキスト)"), rightContainer));
    m_dialogueEdit = new QPlainTextEdit(rightContainer);
    m_dialogueEdit->setFixedHeight(60);
    m_dialogueEdit->setEnabled(false);
    dialogueTextColumn->addWidget(m_dialogueEdit);
    textRow->addLayout(dialogueTextColumn);

    rightLayout->addLayout(textRow);

    mainLayout->addWidget(rightContainer, 4);

    setCentralWidget(central);

    // ストローク完了通知(Undo用コマンドが渡るが絵コンテにUndoはないため受け取って捨てる)
    m_canvas->setStrokeCommandSink(
        [this](std::unique_ptr<core::Command>) { onStrokeFinished(); });
    m_actionCanvas->setStrokeCommandSink(
        [this](std::unique_ptr<core::Command>) { onOverlayStrokeFinished(); });
    m_dialogueCanvas->setStrokeCommandSink(
        [this](std::unique_ptr<core::Command>) { onOverlayStrokeFinished(); });

    connect(m_penButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        for (GLCanvas* canvas : {m_canvas, m_actionCanvas, m_dialogueCanvas}) canvas->setTool(GLCanvas::Tool::Pen);
        m_radiusSlider->setValue(static_cast<int>(m_penRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_penRadius)));
    });
    connect(m_eraserButton, &QPushButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        for (GLCanvas* canvas : {m_canvas, m_actionCanvas, m_dialogueCanvas}) canvas->setTool(GLCanvas::Tool::Eraser);
        m_radiusSlider->setValue(static_cast<int>(m_eraserRadius));
        m_radiusValueLabel->setText(QString::number(static_cast<int>(m_eraserRadius)));
    });
    connect(m_radiusSlider, &QSlider::valueChanged, this, &StoryboardWindow::onRadiusSliderChanged);
    connect(m_colorButton, &QPushButton::clicked, this, &StoryboardWindow::chooseColor);

    connect(m_table, &QTableWidget::itemChanged, this, &StoryboardWindow::onItemChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &StoryboardWindow::onSelectionChanged);

    connect(addButton, &QPushButton::clicked, this, &StoryboardWindow::addPanel);
    connect(removeButton, &QPushButton::clicked, this, &StoryboardWindow::removePanel);
    connect(upButton, &QPushButton::clicked, this, [this] { movePanel(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { movePanel(1); });
    connect(createCutButton, &QPushButton::clicked, this, &StoryboardWindow::createCutFromPanel);
    connect(m_actionEdit, &QPlainTextEdit::textChanged, this, &StoryboardWindow::onActionTextChanged);
    connect(m_dialogueEdit, &QPlainTextEdit::textChanged, this, &StoryboardWindow::onDialogueTextChanged);

    applyToolSettingsToCanvases();

    QToolBar* toolBar = addToolBar(tr("絵コンテ"));
    toolBar->setMovable(false);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &StoryboardWindow::refresh);
    m_totalLabel = new QLabel(toolBar);
    toolBar->addWidget(m_totalLabel);
    updateTotalDurationLabel();
}

void StoryboardWindow::setProject(core::Project* project) {
    m_project = project;
    m_selectedRow = -1;
}

void StoryboardWindow::refresh() {
    if (!m_table) return;
    m_updating = true;

    if (!m_project || m_project->sceneCount() == 0) {
        m_table->setRowCount(0);
        m_selectedRow = -1;
        updateTotalDurationLabel();
        m_updating = false;
        bindCanvasToSelectedPanel();
        return;
    }

    core::Scene& scene = m_project->scene(0);
    auto& panels = scene.storyboard();
    const int count = static_cast<int>(panels.size());
    m_table->setRowCount(count);

    for (int row = 0; row < count; ++row) {
        core::StoryboardPanel& panel = panels[static_cast<size_t>(row)];

        // No(編集不可)
        auto* noItem = new QTableWidgetItem(QString::number(row + 1));
        noItem->setFlags(noItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColNo, noItem);

        // 絵(サムネ、編集不可)
        auto* thumbItem = new QTableWidgetItem();
        thumbItem->setFlags(thumbItem->flags() & ~Qt::ItemIsEditable);
        thumbItem->setData(Qt::DecorationRole, makeThumbnail(panel.drawing));
        m_table->setItem(row, kColThumb, thumbItem);

        // カット番号(編集可)
        m_table->setItem(row, kColCutLabel, new QTableWidgetItem(QString::fromStdString(panel.cutLabel)));
        // 内容(表示専用。複数行対応のため右側のQPlainTextEditで編集する)
        auto* actionItem = new QTableWidgetItem(QString::fromStdString(panel.action));
        actionItem->setFlags(actionItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColAction, actionItem);
        // セリフ(表示専用。複数行対応のため右側のQPlainTextEditで編集する)
        auto* dialogueItem = new QTableWidgetItem(QString::fromStdString(panel.dialogue));
        dialogueItem->setFlags(dialogueItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColDialogue, dialogueItem);
        // 尺コマ(編集可、数値)
        m_table->setItem(row, kColDuration, new QTableWidgetItem(QString::number(panel.durationFrames)));
    }

    m_table->resizeRowsToContents();
    for (int row = 0; row < count; ++row) {
        m_table->setRowHeight(row, kRowHeight);
    }

    if (count > 0) {
        m_selectedRow = std::clamp(m_selectedRow, 0, count - 1);
        m_table->setCurrentCell(m_selectedRow, kColCutLabel);  // m_updating中なのでonSelectionChangedは無視される
    } else {
        m_selectedRow = -1;
    }

    updateTotalDurationLabel();
    m_updating = false;

    // 構造変更後は古いテクスチャを破棄し、vectorの再配置に備えて必ず選択パネルへ再設定する
    m_canvas->clearTextureCache();
    m_actionCanvas->clearTextureCache();
    m_dialogueCanvas->clearTextureCache();
    bindCanvasToSelectedPanel();
}

void StoryboardWindow::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || !m_project || !item) return;
    if (m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    const int row = item->row();
    if (row < 0 || static_cast<size_t>(row) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(row)];

    switch (item->column()) {
        case kColCutLabel:
            panel.cutLabel = item->text().toStdString();
            emit edited();
            break;
        case kColDuration: {
            bool ok = false;
            const int value = item->text().toInt(&ok);
            if (!ok || value <= 0) {
                // 無効入力は元に戻す
                m_updating = true;
                item->setText(QString::number(panel.durationFrames));
                m_updating = false;
                return;
            }
            panel.durationFrames = static_cast<size_t>(value);
            updateTotalDurationLabel();
            emit edited();
            break;
        }
        default:
            break;  // No/絵/内容/セリフは編集不可のため到達しない
    }
}

void StoryboardWindow::onSelectionChanged() {
    if (m_updating) return;
    const int row = selectedPanelIndex();
    if (row < 0) return;
    m_selectedRow = row;
    bindCanvasToSelectedPanel();
}

int StoryboardWindow::selectedPanelIndex() const {
    return m_table ? m_table->currentRow() : -1;
}

void StoryboardWindow::addPanel() {
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();

    core::StoryboardPanel panel;
    panel.drawing = core::Bitmap(kPanelWidth, kPanelHeight);
    panel.drawing.fill({0, 0, 0, 0});
    panel.cutLabel = panels.empty() ? std::string("1") : panels.back().cutLabel;
    panels.push_back(std::move(panel));

    m_selectedRow = static_cast<int>(panels.size()) - 1;
    refresh();
    emit edited();
}

void StoryboardWindow::removePanel() {
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    const int row = selectedPanelIndex();
    if (row < 0 || static_cast<size_t>(row) >= panels.size()) return;

    panels.erase(panels.begin() + row);
    m_selectedRow = std::min(row, static_cast<int>(panels.size()) - 1);
    refresh();
    emit edited();
}

void StoryboardWindow::movePanel(int delta) {
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    const int row = selectedPanelIndex();
    const int target = row + delta;
    if (row < 0 || target < 0 || static_cast<size_t>(target) >= panels.size()) return;

    std::swap(panels[static_cast<size_t>(row)], panels[static_cast<size_t>(target)]);
    m_selectedRow = target;
    refresh();
    emit edited();
}

void StoryboardWindow::createCutFromPanel() {
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    const int row = selectedPanelIndex();
    if (row < 0 || static_cast<size_t>(row) >= panels.size()) return;

    // 1カット複数コマの尺を合算する仕様: 同じカット番号を持つ全パネルのduration合計を尺とする
    const std::string label = panels[static_cast<size_t>(row)].cutLabel;
    int totalFrames = 0;
    for (const auto& panel : panels) {
        if (panel.cutLabel == label) totalFrames += static_cast<int>(panel.durationFrames);
    }
    emit createCutRequested(QString::fromStdString(label), totalFrames);
}

void StoryboardWindow::onStrokeFinished() {
    const int row = selectedPanelIndex();
    if (row >= 0) updateThumbnail(row);
    emit edited();
}

void StoryboardWindow::onOverlayStrokeFinished() {
    // 内容欄/セリフ欄の手書きはサムネイル対象外(サムネはdrawingのみ)だが、編集通知は発火する
    emit edited();
}

void StoryboardWindow::updateThumbnail(int row) {
    if (!m_project || m_project->sceneCount() == 0 || !m_table) return;
    auto& panels = m_project->scene(0).storyboard();
    if (row < 0 || static_cast<size_t>(row) >= panels.size()) return;
    QTableWidgetItem* item = m_table->item(row, kColThumb);
    if (!item) return;

    m_updating = true;
    item->setData(Qt::DecorationRole, makeThumbnail(panels[static_cast<size_t>(row)].drawing));
    m_updating = false;
}

void StoryboardWindow::bindCanvasToSelectedPanel() {
    if (!m_project || m_project->sceneCount() == 0) {
        m_canvas->setBitmap(nullptr);
        m_actionCanvas->setBitmap(nullptr);
        m_dialogueCanvas->setBitmap(nullptr);
        m_updating = true;
        m_actionEdit->clear();
        m_dialogueEdit->clear();
        m_updating = false;
        m_actionEdit->setEnabled(false);
        m_dialogueEdit->setEnabled(false);
        return;
    }
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) {
        m_canvas->setBitmap(nullptr);
        m_actionCanvas->setBitmap(nullptr);
        m_dialogueCanvas->setBitmap(nullptr);
        m_updating = true;
        m_actionEdit->clear();
        m_dialogueEdit->clear();
        m_updating = false;
        m_actionEdit->setEnabled(false);
        m_dialogueEdit->setEnabled(false);
        return;
    }

    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    if (panel.drawing.isEmpty()) {
        panel.drawing = core::Bitmap(kPanelWidth, kPanelHeight);
        panel.drawing.fill({0, 0, 0, 0});
    }
    m_canvas->setBitmap(&panel.drawing);

    // 内容欄/セリフ欄への手書きは遅延初期化(isEmptyなら400x540透明で確保)
    if (panel.actionDrawing.isEmpty()) {
        panel.actionDrawing = core::Bitmap(kMemoWidth, kMemoHeight);
        panel.actionDrawing.fill({0, 0, 0, 0});
    }
    m_actionCanvas->setBitmap(&panel.actionDrawing);
    if (panel.dialogueDrawing.isEmpty()) {
        panel.dialogueDrawing = core::Bitmap(kMemoWidth, kMemoHeight);
        panel.dialogueDrawing.fill({0, 0, 0, 0});
    }
    m_dialogueCanvas->setBitmap(&panel.dialogueDrawing);

    // 内容/セリフ(複数行テキスト)を読み込む。m_updatingガードでtextChangedの暴発を防ぐ
    m_updating = true;
    m_actionEdit->setPlainText(QString::fromStdString(panel.action));
    m_dialogueEdit->setPlainText(QString::fromStdString(panel.dialogue));
    m_updating = false;
    m_actionEdit->setEnabled(true);
    m_dialogueEdit->setEnabled(true);

    // テキストをキャンバスの下敷きに敷く。テキストの上に手書きで重ね書きできるようにするため
    m_actionCanvas->setUnderlayImage(renderTextUnderlay(m_actionEdit->toPlainText()));
    m_actionCanvas->setUnderlayOpacity(1.0f);
    m_dialogueCanvas->setUnderlayImage(renderTextUnderlay(m_dialogueEdit->toPlainText()));
    m_dialogueCanvas->setUnderlayOpacity(1.0f);
}

void StoryboardWindow::onRadiusSliderChanged(int value) {
    if (m_penButton->isChecked()) {
        m_penRadius = static_cast<float>(value);
    } else {
        m_eraserRadius = static_cast<float>(value);
    }
    m_radiusValueLabel->setText(QString::number(value));
    applyToolSettingsToCanvases();
}

void StoryboardWindow::chooseColor() {
    const QColor chosen = QColorDialog::getColor(m_penColor, this, tr("ペンの色"));
    if (!chosen.isValid()) return;
    m_penColor = chosen;
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
    applyToolSettingsToCanvases();
}

void StoryboardWindow::applyToolSettingsToCanvases() {
    // ペン/消しゴムの太さ・色設定は絵キャンバス・内容欄・セリフ欄の3キャンバスへ同時に適用する
    for (GLCanvas* canvas : {m_canvas, m_actionCanvas, m_dialogueCanvas}) {
        canvas->setPenRadius(m_penRadius);
        canvas->setPenColor(m_penColor);
        canvas->setEraserRadius(m_eraserRadius);
    }
}

void StoryboardWindow::onActionTextChanged() {
    if (m_updating || !m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;

    panels[static_cast<size_t>(m_selectedRow)].action = m_actionEdit->toPlainText().toStdString();
    m_updating = true;
    if (QTableWidgetItem* item = m_table->item(m_selectedRow, kColAction)) {
        item->setText(m_actionEdit->toPlainText());
    }
    m_updating = false;
    // テキストの上に手書きで重ね書きできるよう、下敷きを最新のテキストで敷き直す
    m_actionCanvas->setUnderlayImage(renderTextUnderlay(m_actionEdit->toPlainText()));
    m_actionCanvas->setUnderlayOpacity(1.0f);
    emit edited();
}

void StoryboardWindow::onDialogueTextChanged() {
    if (m_updating || !m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;

    panels[static_cast<size_t>(m_selectedRow)].dialogue = m_dialogueEdit->toPlainText().toStdString();
    m_updating = true;
    if (QTableWidgetItem* item = m_table->item(m_selectedRow, kColDialogue)) {
        item->setText(m_dialogueEdit->toPlainText());
    }
    m_updating = false;
    // テキストの上に手書きで重ね書きできるよう、下敷きを最新のテキストで敷き直す
    m_dialogueCanvas->setUnderlayImage(renderTextUnderlay(m_dialogueEdit->toPlainText()));
    m_dialogueCanvas->setUnderlayOpacity(1.0f);
    emit edited();
}

void StoryboardWindow::updateTotalDurationLabel() {
    if (!m_totalLabel) return;
    size_t total = 0;
    if (m_project && m_project->sceneCount() > 0) {
        for (const auto& panel : m_project->scene(0).storyboard()) total += panel.durationFrames;
    }
    const double seconds = static_cast<double>(total) / kFps;
    m_totalLabel->setText(tr(" 合計: %1コマ (%2秒) ").arg(total).arg(seconds, 0, 'f', 2));
}
