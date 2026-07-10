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
// 「よくあるコンテ用紙テンプレート」を模した1枚の紙のサイズ。全カット共通。
// この紙全体を1つの手書きビットマップ(StoryboardPanel::drawing)が覆う
constexpr int kSheetWidth = 1920;
constexpr int kSheetHeight = 600;

// 左→右のセルの境界(縦罫線)のx座標
constexpr int kNoColX0 = 0;      // カットNo欄
constexpr int kNoColX1 = 120;
constexpr int kPictureColX1 = 1100;  // 画面(絵)欄
constexpr int kActionColX1 = 1500;   // 内容欄
constexpr int kDialogueColX1 = 1820;  // セリフ欄
constexpr int kSecColX1 = kSheetWidth;  // 秒欄

// 画面(絵)欄の中の16:9フレーム枠(絵はこの枠内に描く想定)
const QRect kFrameRect(130, 30, 960, 540);

constexpr double kFps = 24.0;  // タイムシートは24fps基準

// サムネイル解像度(表示は列幅に合わせて縮小する)
constexpr int kThumbWidth = 96;
constexpr int kThumbHeight = 54;
constexpr int kRowHeight = 70;

// 太さスライダーの範囲(ペン/消しゴム共通)
constexpr int kRadiusMin = 1;
constexpr int kRadiusMax = 40;

// 罫線・見出しの色(濃いグレー)と余白/フォントサイズ
const QColor kLineColor(0x44, 0x44, 0x44);
constexpr int kMargin = 8;
constexpr int kHeadingPixelSize = 14;
constexpr int kBodyPixelSize = 20;
constexpr int kHeadingHeight = 20;

enum Column {
    kColNo = 0,
    kColThumb,
    kColCutLabel,
    kColAction,
    kColDialogue,
    kColDuration,
    kColCount,
};

// サムネイルは「画面(絵)欄」のフレーム枠(kFrameRect)部分だけを切り出し、白背景に合成してから
// サムネイルサイズへ縮小する。透明のまま縮小するとデコレーション表示上は黒く見えてしまうため
QPixmap makeThumbnail(const core::Bitmap& bitmap) {
    if (bitmap.isEmpty()) return QPixmap();
    const QImage source(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    const QImage cropped = source.copy(kFrameRect);
    QImage composed(cropped.width(), cropped.height(), QImage::Format_RGB32);
    composed.fill(Qt::white);
    QPainter painter(&composed);
    painter.drawImage(0, 0, cropped);
    painter.end();
    // drawImage()は即座にピクセルをコピーするためsource/bitmap破棄後も安全
    return QPixmap::fromImage(
        composed.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// コンテ用紙1枚分の下敷き(罫線・見出し・カット番号・内容/セリフの印字テキスト・尺)をQImageに
// 描画する。GLCanvasの下敷きとして敷き、この上に手書きインク(drawing)を重ねる。
// panelNoは1始まりのパネル通し番号(カットNo欄の見出しに使う)
QImage renderSheetUnderlay(const core::StoryboardPanel& panel, int panelNo) {
    QImage image(kSheetWidth, kSheetHeight, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    QPen linePen(kLineColor);
    linePen.setWidth(1);
    painter.setPen(linePen);

    // 外枠+セルを区切る縦罫線
    painter.drawRect(QRect(0, 0, kSheetWidth - 1, kSheetHeight - 1));
    for (int x : {kNoColX1, kPictureColX1, kActionColX1, kDialogueColX1}) {
        painter.drawLine(x, 0, x, kSheetHeight);
    }

    QFont headingFont = painter.font();
    headingFont.setPixelSize(kHeadingPixelSize);
    QFont bodyFont = painter.font();
    bodyFont.setPixelSize(kBodyPixelSize);

    QTextOption wrapOption;
    wrapOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    // カットNo欄: 上部に小さく「C#<パネル通し番号>」見出し、中央にカット番号を大きめに印字
    {
        painter.setPen(kLineColor);
        painter.setFont(headingFont);
        const QRect headRect(kNoColX0 + kMargin, kMargin, (kNoColX1 - kNoColX0) - kMargin * 2, kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("C#%1").arg(panelNo));

        QFont cutFont = painter.font();
        cutFont.setPixelSize(28);
        painter.setFont(cutFont);
        painter.setPen(Qt::black);
        const QRect cutRect(kNoColX0, kHeadingHeight, kNoColX1 - kNoColX0, kSheetHeight - kHeadingHeight);
        painter.drawText(cutRect, Qt::AlignCenter, QString::fromStdString(panel.cutLabel));
    }

    // 画面(絵)欄: 16:9フレーム枠を実線で印字(絵はこの枠内に描く想定)
    {
        painter.setPen(kLineColor);
        painter.drawRect(kFrameRect);
    }

    // 内容欄: 見出し+内容テキストの折り返し印字
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(kPictureColX1 + kMargin, kMargin, (kActionColX1 - kPictureColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("内容"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const QRect bodyRect(kPictureColX1 + kMargin, kHeadingHeight + kMargin,
                              (kActionColX1 - kPictureColX1) - kMargin * 2,
                              kSheetHeight - kHeadingHeight - kMargin * 2);
        painter.drawText(bodyRect, QString::fromStdString(panel.action), wrapOption);
    }

    // セリフ欄: 見出し+セリフテキストの折り返し印字
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(kActionColX1 + kMargin, kMargin, (kDialogueColX1 - kActionColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("セリフ"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const QRect bodyRect(kActionColX1 + kMargin, kHeadingHeight + kMargin,
                              (kDialogueColX1 - kActionColX1) - kMargin * 2,
                              kSheetHeight - kHeadingHeight - kMargin * 2);
        painter.drawText(bodyRect, QString::fromStdString(panel.dialogue), wrapOption);
    }

    // 秒欄: 見出し+尺コマ数と秒数(例「36k」「1.5s」)
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(kDialogueColX1 + kMargin, kMargin, (kSecColX1 - kDialogueColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("秒"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const double seconds = static_cast<double>(panel.durationFrames) / kFps;
        const QString text = QStringLiteral("%1k\n%2s").arg(panel.durationFrames).arg(seconds, 0, 'f', 1);
        const QRect bodyRect(kDialogueColX1, kHeadingHeight, kSecColX1 - kDialogueColX1,
                              kSheetHeight - kHeadingHeight);
        painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop, text);
    }

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

    // コンテ用紙1枚(罫線・見出し・カット番号・絵の枠・内容欄・セリフ欄・秒欄を印字した下敷きの
    // 上に手書きインクを重ねる、紙全体をカバーする1つのGLCanvas)
    m_canvas = new GLCanvas(rightContainer);
    m_canvas->setCanvasSize(kSheetWidth, kSheetHeight);
    m_canvas->setTool(GLCanvas::Tool::Pen);
    rightLayout->addWidget(m_canvas, 2);

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
            // カット番号はコンテ用紙のカットNo欄に印字されるため、選択中パネルなら下敷きを敷き直す
            if (row == m_selectedRow) {
                m_canvas->setUnderlayImage(renderSheetUnderlay(panel, row + 1));
                m_canvas->setUnderlayOpacity(1.0f);
            }
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
            // 尺コマ数はコンテ用紙の秒欄に印字されるため、選択中パネルなら下敷きを敷き直す
            if (row == m_selectedRow) {
                m_canvas->setUnderlayImage(renderSheetUnderlay(panel, row + 1));
                m_canvas->setUnderlayOpacity(1.0f);
            }
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
    panel.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
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
        m_updating = true;
        m_actionEdit->clear();
        m_dialogueEdit->clear();
        m_updating = false;
        m_actionEdit->setEnabled(false);
        m_dialogueEdit->setEnabled(false);
        return;
    }

    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    // drawingはコンテ用紙全体(kSheetWidth x kSheetHeight)を覆う1枚の手書きビットマップ。
    // 未確保またはサイズ不一致なら確保し直す(開発中につき旧サイズのデータは破棄してよい)
    if (panel.drawing.isEmpty() || panel.drawing.width() != kSheetWidth ||
        panel.drawing.height() != kSheetHeight) {
        panel.drawing = core::Bitmap(kSheetWidth, kSheetHeight);
        panel.drawing.fill({0, 0, 0, 0});
    }
    m_canvas->setBitmap(&panel.drawing);

    // 内容/セリフ(複数行テキスト)を読み込む。m_updatingガードでtextChangedの暴発を防ぐ
    m_updating = true;
    m_actionEdit->setPlainText(QString::fromStdString(panel.action));
    m_dialogueEdit->setPlainText(QString::fromStdString(panel.dialogue));
    m_updating = false;
    m_actionEdit->setEnabled(true);
    m_dialogueEdit->setEnabled(true);

    // コンテ用紙の下敷き(罫線・見出し・カット番号・内容/セリフ・秒)を敷く。
    // 手書きインク(drawing)はこの上に重なる
    m_canvas->setUnderlayImage(renderSheetUnderlay(panel, m_selectedRow + 1));
    m_canvas->setUnderlayOpacity(1.0f);
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
    // ペン/消しゴムの太さ・色設定はコンテ用紙キャンバスへ適用する
    m_canvas->setPenRadius(m_penRadius);
    m_canvas->setPenColor(m_penColor);
    m_canvas->setEraserRadius(m_eraserRadius);
}

void StoryboardWindow::onActionTextChanged() {
    if (m_updating || !m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;

    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    panel.action = m_actionEdit->toPlainText().toStdString();
    m_updating = true;
    if (QTableWidgetItem* item = m_table->item(m_selectedRow, kColAction)) {
        item->setText(m_actionEdit->toPlainText());
    }
    m_updating = false;
    // コンテ用紙の下敷きを最新の内容テキストで敷き直す(手書きインクはそのまま重なる)
    m_canvas->setUnderlayImage(renderSheetUnderlay(panel, m_selectedRow + 1));
    m_canvas->setUnderlayOpacity(1.0f);
    emit edited();
}

void StoryboardWindow::onDialogueTextChanged() {
    if (m_updating || !m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;

    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    panel.dialogue = m_dialogueEdit->toPlainText().toStdString();
    m_updating = true;
    if (QTableWidgetItem* item = m_table->item(m_selectedRow, kColDialogue)) {
        item->setText(m_dialogueEdit->toPlainText());
    }
    m_updating = false;
    // コンテ用紙の下敷きを最新のセリフテキストで敷き直す(手書きインクはそのまま重なる)
    m_canvas->setUnderlayImage(renderSheetUnderlay(panel, m_selectedRow + 1));
    m_canvas->setUnderlayOpacity(1.0f);
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
