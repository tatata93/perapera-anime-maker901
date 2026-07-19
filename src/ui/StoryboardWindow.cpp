#include "StoryboardWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMarginsF>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTextOption>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Project.h"
#include "core/StrokeCommand.h"
#include "render/GLCanvas.h"
#include "ui/CanvasSizeDialog.h"
#include "ui/FloatingCanvasWindow.h"
#include "ui/LayerPanel.h"
#include "ui/PaintLayerUtils.h"
#include "ui/RetroTheme.h"

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
int scaleCoord(int value, int from, int to) {
    if (from <= 0) return value;
    return static_cast<int>(std::lround(static_cast<double>(value) * to / from));
}

QRect scaledFrameRect(int width, int height) {
    return QRect(scaleCoord(kFrameRect.x(), kSheetWidth, width), scaleCoord(kFrameRect.y(), kSheetHeight, height),
                 scaleCoord(kFrameRect.width(), kSheetWidth, width),
                 scaleCoord(kFrameRect.height(), kSheetHeight, height));
}

QSize panelCanvasSize(const core::StoryboardPanel& panel) {
    return perapera::ui::paintLayerCanvasSize(panel.layers, panel.drawing, kSheetWidth, kSheetHeight);
}

QImage panelDrawingImage(const core::StoryboardPanel& panel) {
    const QSize size = panelCanvasSize(panel);
    if (!panel.layers.empty()) {
        return perapera::ui::bitmapToImageCopy(
            perapera::ui::compositePaintLayers(panel.layers, size.width(), size.height()));
    }
    return perapera::ui::bitmapToImageCopy(panel.drawing);
}

void syncStoryboardPanelComposite(core::StoryboardPanel& panel) {
    const QSize size = panelCanvasSize(panel);
    perapera::ui::ensurePaintLayers(panel.layers, panel.activeLayer, panel.drawing, size.width(), size.height(), true);
    panel.drawing = perapera::ui::compositePaintLayers(panel.layers, size.width(), size.height());
}

QPixmap makeThumbnail(const core::StoryboardPanel& panel) {
    const QImage source = panelDrawingImage(panel);
    if (source.isNull()) return QPixmap();
    const QImage cropped = source.copy(scaledFrameRect(source.width(), source.height()));
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
    const QSize size = panelCanvasSize(panel);
    const int sheetWidth = std::max(1, size.width());
    const int sheetHeight = std::max(1, size.height());
    const int noColX0 = 0;
    const int noColX1 = scaleCoord(kNoColX1, kSheetWidth, sheetWidth);
    const int pictureColX1 = scaleCoord(kPictureColX1, kSheetWidth, sheetWidth);
    const int actionColX1 = scaleCoord(kActionColX1, kSheetWidth, sheetWidth);
    const int dialogueColX1 = scaleCoord(kDialogueColX1, kSheetWidth, sheetWidth);
    const int secColX1 = sheetWidth;
    const QRect frameRect = scaledFrameRect(sheetWidth, sheetHeight);

    QImage image(sheetWidth, sheetHeight, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    QPen linePen(kLineColor);
    linePen.setWidth(1);
    painter.setPen(linePen);

    // 外枠+セルを区切る縦罫線
    painter.drawRect(QRect(0, 0, sheetWidth - 1, sheetHeight - 1));
    for (int x : {noColX1, pictureColX1, actionColX1, dialogueColX1}) {
        painter.drawLine(x, 0, x, sheetHeight);
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
        const QRect headRect(noColX0 + kMargin, kMargin, (noColX1 - noColX0) - kMargin * 2, kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("C#%1").arg(panelNo));

        QFont cutFont = painter.font();
        cutFont.setPixelSize(28);
        painter.setFont(cutFont);
        painter.setPen(Qt::black);
        const QRect cutRect(noColX0, kHeadingHeight, noColX1 - noColX0, sheetHeight - kHeadingHeight);
        painter.drawText(cutRect, Qt::AlignCenter, QString::fromStdString(panel.cutLabel));
    }

    // 画面(絵)欄: 16:9フレーム枠を実線で印字(絵はこの枠内に描く想定)
    {
        painter.setPen(kLineColor);
        painter.drawRect(frameRect);
    }

    // 内容欄: 見出し+内容テキストの折り返し印字
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(pictureColX1 + kMargin, kMargin, (actionColX1 - pictureColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("内容"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const QRect bodyRect(pictureColX1 + kMargin, kHeadingHeight + kMargin,
                              (actionColX1 - pictureColX1) - kMargin * 2,
                              sheetHeight - kHeadingHeight - kMargin * 2);
        painter.drawText(bodyRect, QString::fromStdString(panel.action), wrapOption);
    }

    // セリフ欄: 見出し+セリフテキストの折り返し印字
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(actionColX1 + kMargin, kMargin, (dialogueColX1 - actionColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("セリフ"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const QRect bodyRect(actionColX1 + kMargin, kHeadingHeight + kMargin,
                              (dialogueColX1 - actionColX1) - kMargin * 2,
                              sheetHeight - kHeadingHeight - kMargin * 2);
        painter.drawText(bodyRect, QString::fromStdString(panel.dialogue), wrapOption);
    }

    // 秒欄: 見出し+尺コマ数と秒数(例「36k」「1.5s」)
    {
        painter.setFont(headingFont);
        painter.setPen(kLineColor);
        const QRect headRect(dialogueColX1 + kMargin, kMargin, (secColX1 - dialogueColX1) - kMargin * 2,
                              kHeadingHeight);
        painter.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("秒"));

        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        const double seconds = static_cast<double>(panel.durationFrames) / kFps;
        const QString text = QStringLiteral("%1k\n%2s").arg(panel.durationFrames).arg(seconds, 0, 'f', 1);
        const QRect bodyRect(dialogueColX1, kHeadingHeight, secColX1 - dialogueColX1, sheetHeight - kHeadingHeight);
        painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop, text);
    }

    painter.end();
    return image;
}

// 絵の枠(kFrameRect)部分を白背景に合成したQImageを返す。絵が未描画(枠内が全面透明)なら
// 白紙のまま中央にカット番号(panelNo、1始まり)を薄く重ねる。プレビュー再生の1コマ分の絵に使う
QImage composeFrameImage(const core::StoryboardPanel& panel, int panelNo) {
    const QImage source = panelDrawingImage(panel);
    const QRect frameRect = source.isNull() ? kFrameRect : scaledFrameRect(source.width(), source.height());
    QImage composed(frameRect.size(), QImage::Format_RGB32);
    composed.fill(Qt::white);

    bool blank = true;
    if (!source.isNull()) {
        const QImage cropped = source.copy(frameRect);
        for (int y = 0; y < cropped.height() && blank; ++y) {
            const uchar* row = cropped.constScanLine(y);
            for (int x = 0; x < cropped.width(); ++x) {
                if (row[x * 4 + 3] != 0) {
                    blank = false;
                    break;
                }
            }
        }
        QPainter painter(&composed);
        painter.drawImage(0, 0, cropped);
        painter.end();
    }

    if (blank) {
        QPainter painter(&composed);
        QFont font = painter.font();
        font.setPixelSize(64);
        painter.setFont(font);
        painter.setPen(QColor(0, 0, 0, 60));
        painter.drawText(composed.rect(), Qt::AlignCenter, QStringLiteral("C#%1").arg(panelNo));
        painter.end();
    }
    return composed;
}

// プレビュー(ビデオコンテ)再生ダイアログ。各パネルの絵をdurationFrames(24fps)どおりの時間だけ
// 順番に表示し、最後まで行ったら先頭へループする。モードレスで、閉じるとタイマーを止める
QImage renderStoryboardSheetImage(const core::StoryboardPanel& panel, int panelNo) {
    QImage sheet = renderSheetUnderlay(panel, panelNo).convertToFormat(QImage::Format_RGB32);
    const QImage drawing = panelDrawingImage(panel);
    if (!drawing.isNull()) {
        QPainter painter(&sheet);
        painter.drawImage(0, 0, drawing);
        painter.end();
    }
    return sheet;
}

class StoryboardPreviewDialog : public QDialog {
public:
    StoryboardPreviewDialog(core::Project* project, QWidget* parent) : QDialog(parent), m_project(project) {
        setWindowTitle(QObject::tr("コンテプレビュー"));
        resize(990, 640);

        auto* layout = new QVBoxLayout(this);
        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setMinimumSize(320, 180);
        m_imageLabel->setStyleSheet(QStringLiteral("background-color: black;"));
        layout->addWidget(m_imageLabel, 1);

        auto* bottomRow = new QHBoxLayout();
        m_playButton = new QPushButton(QObject::tr("一時停止"), this);
        bottomRow->addWidget(m_playButton);
        m_infoLabel = new QLabel(this);
        bottomRow->addWidget(m_infoLabel);
        bottomRow->addStretch();
        layout->addLayout(bottomRow);

        connect(m_playButton, &QPushButton::clicked, this, [this] {
            m_playing = !m_playing;
            m_playButton->setText(m_playing ? QObject::tr("一時停止") : QObject::tr("再生"));
        });

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] { onTick(); });
        m_timer->start(static_cast<int>(1000.0 / kFps));

        m_row = 0;
        m_frameInPanel = 0;
        m_playing = true;
        updateComposedImage();
        updateInfoLabel();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        applyScaledPixmap();
    }

    void closeEvent(QCloseEvent* event) override {
        m_timer->stop();
        QDialog::closeEvent(event);
    }

private:
    const std::vector<core::StoryboardPanel>* panels() const {
        if (!m_project || m_project->sceneCount() == 0) return nullptr;
        return &m_project->scene(0).storyboard();
    }

    void onTick() {
        const std::vector<core::StoryboardPanel>* list = panels();
        if (!list || list->empty()) return;
        if (!m_playing) return;

        ++m_frameInPanel;
        const size_t duration = std::max<size_t>(1, (*list)[static_cast<size_t>(m_row)].durationFrames);
        if (static_cast<size_t>(m_frameInPanel) >= duration) {
            m_frameInPanel = 0;
            m_row = (m_row + 1) % static_cast<int>(list->size());  // 最後まで行ったら先頭へループ
            updateComposedImage();
        }
        updateInfoLabel();
    }

    // パネルが変わった時だけ絵を合成し直す(毎フレームの再合成はしない)
    void updateComposedImage() {
        const std::vector<core::StoryboardPanel>* list = panels();
        if (!list || list->empty() || m_row < 0 || static_cast<size_t>(m_row) >= list->size()) {
            m_composedImage = QImage();
            applyScaledPixmap();
            return;
        }
        m_composedImage = composeFrameImage((*list)[static_cast<size_t>(m_row)], m_row + 1);
        applyScaledPixmap();
    }

    void applyScaledPixmap() {
        if (m_composedImage.isNull()) {
            m_imageLabel->setPixmap(QPixmap());
            return;
        }
        m_imageLabel->setPixmap(QPixmap::fromImage(
            m_composedImage.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }

    void updateInfoLabel() {
        const std::vector<core::StoryboardPanel>* list = panels();
        if (!list || list->empty()) {
            m_infoLabel->setText(QObject::tr("パネルなし"));
            return;
        }
        const core::StoryboardPanel& panel = (*list)[static_cast<size_t>(m_row)];
        const double elapsedSec = static_cast<double>(m_frameInPanel) / kFps;
        const double totalSec = static_cast<double>(panel.durationFrames) / kFps;
        m_infoLabel->setText(QObject::tr("パネル %1/%2  カット%3  %4s / %5s")
                                  .arg(m_row + 1)
                                  .arg(list->size())
                                  .arg(QString::fromStdString(panel.cutLabel))
                                  .arg(elapsedSec, 0, 'f', 1)
                                  .arg(totalSec, 0, 'f', 1));
    }

    core::Project* m_project;
    QLabel* m_imageLabel = nullptr;
    QLabel* m_infoLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QTimer* m_timer = nullptr;
    QImage m_composedImage;  // 現在パネルの合成済み絵(白背景+絵の枠切り出し)。パネル変更時のみ更新
    int m_row = 0;
    int m_frameInPanel = 0;
    bool m_playing = true;
};

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

    // 絵の枠(kFrameRect)の拡大表示トグル(枠のダブルクリックと同じ効果。ボタンでも切替可能)
    m_zoomButton = new QPushButton(tr("絵を拡大"), rightContainer);
    m_zoomButton->setCheckable(true);
    toolRow->addWidget(m_zoomButton);
    auto* resizeButton = new QPushButton(tr("サイズ"), rightContainer);
    toolRow->addWidget(resizeButton);
    auto* exportButton = new QPushButton(tr("A4書き出し"), rightContainer);
    toolRow->addWidget(exportButton);
    auto* detachButton = new QPushButton(tr("別窓"), rightContainer);
    toolRow->addWidget(detachButton);

    toolRow->addStretch();
    rightLayout->addLayout(toolRow);

    // コンテ用紙1枚(罫線・見出し・カット番号・絵の枠・内容欄・セリフ欄・秒欄を印字した下敷きの
    // 上に手書きインクを重ねる、紙全体をカバーする1つのGLCanvas)
    m_canvasHost = new QWidget(rightContainer);
    m_canvasLayout = new QVBoxLayout(m_canvasHost);
    m_canvasLayout->setContentsMargins(0, 0, 0, 0);
    m_canvas = createCanvas(m_canvasHost);
    m_canvasLayout->addWidget(m_canvas);
    rightLayout->addWidget(m_canvasHost, 2);

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
    m_layerPanel = new LayerPanel(this);
    m_layerPanel->setWindowTitle(tr("絵コンテ レイヤー"));
    addDockWidget(Qt::RightDockWidgetArea, m_layerPanel);

    // ストローク完了通知(Undo用コマンドが渡る。絵コンテに通常のUndo操作はないが、絵の枠の
    // ダブルクリング1回目で打たれてしまう点を取り消すために直近のコマンドだけ保持しておく)
    connect(m_zoomButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked == m_frameZoomed) return;  // トグル側からの反映(setChecked)による再帰を防ぐ
        m_frameZoomed = checked;
        if (m_frameZoomed) {
            QRect frameRect = kFrameRect;
            if (m_project && m_project->sceneCount() > 0 && m_selectedRow >= 0) {
                auto& panels = m_project->scene(0).storyboard();
                if (static_cast<size_t>(m_selectedRow) < panels.size()) {
                    const QSize size = panelCanvasSize(panels[static_cast<size_t>(m_selectedRow)]);
                    frameRect = scaledFrameRect(size.width(), size.height());
                }
            }
            if (m_canvas) m_canvas->zoomToCanvasRect(frameRect);
        } else {
            if (m_canvas) m_canvas->resetView();
        }
    });

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
    connect(m_radiusSlider, &QSlider::valueChanged, this, &StoryboardWindow::onRadiusSliderChanged);
    connect(m_colorButton, &QPushButton::clicked, this, &StoryboardWindow::chooseColor);
    connect(resizeButton, &QPushButton::clicked, this, &StoryboardWindow::resizeStoryboardCanvas);
    connect(exportButton, &QPushButton::clicked, this, &StoryboardWindow::exportStoryboardPdf);
    connect(detachButton, &QPushButton::clicked, this, &StoryboardWindow::detachCanvas);

    connect(m_table, &QTableWidget::itemChanged, this, &StoryboardWindow::onItemChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &StoryboardWindow::onSelectionChanged);

    connect(addButton, &QPushButton::clicked, this, &StoryboardWindow::addPanel);
    connect(removeButton, &QPushButton::clicked, this, &StoryboardWindow::removePanel);
    connect(upButton, &QPushButton::clicked, this, [this] { movePanel(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { movePanel(1); });
    connect(createCutButton, &QPushButton::clicked, this, &StoryboardWindow::createCutFromPanel);
    connect(m_actionEdit, &QPlainTextEdit::textChanged, this, &StoryboardWindow::onActionTextChanged);
    connect(m_dialogueEdit, &QPlainTextEdit::textChanged, this, &StoryboardWindow::onDialogueTextChanged);

    connect(m_layerPanel, &LayerPanel::layerSelected, this, [this](int layerIndex) {
        if (!m_project || m_project->sceneCount() == 0) return;
        auto& panels = m_project->scene(0).storyboard();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;
        core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;
        panel.activeLayer = static_cast<size_t>(layerIndex);
        bindCanvasToSelectedPanel();
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::visibilityChanged, this, [this](int layerIndex, bool visible) {
        if (!m_project || m_project->sceneCount() == 0) return;
        auto& panels = m_project->scene(0).storyboard();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;
        core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;
        panel.layers[static_cast<size_t>(layerIndex)].visible = visible;
        syncSelectedPanelComposite();
        bindCanvasToSelectedPanel();
        updateThumbnail(m_selectedRow);
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::opacityChanged, this, [this](int layerIndex, int opacityPercent) {
        if (!m_project || m_project->sceneCount() == 0) return;
        auto& panels = m_project->scene(0).storyboard();
        if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) return;
        core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
        if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;
        panel.layers[static_cast<size_t>(layerIndex)].setOpacity(std::clamp(opacityPercent, 0, 100) / 100.0);
        syncSelectedPanelComposite();
        bindCanvasToSelectedPanel();
        updateThumbnail(m_selectedRow);
        emit edited();
    });
    connect(m_layerPanel, &LayerPanel::addRequested, this,
            [this] { addPaintLayer(core::LayerRole::Normal); });
    connect(m_layerPanel, &LayerPanel::duplicateRequested, this, &StoryboardWindow::duplicatePaintLayer);
    connect(m_layerPanel, &LayerPanel::removeRequested, this, &StoryboardWindow::removePaintLayer);
    connect(m_layerPanel, &LayerPanel::moveUpRequested, this, [this] { movePaintLayer(1); });
    connect(m_layerPanel, &LayerPanel::moveDownRequested, this, [this] { movePaintLayer(-1); });
    connect(m_layerPanel, &LayerPanel::renameRequested, this, &StoryboardWindow::renamePaintLayer);
    connect(m_layerPanel, &LayerPanel::roleChangeRequested, this, [this](int layerIndex, int role) {
        core::LayerRole layerRole = core::LayerRole::Normal;
        if (role == 1) layerRole = core::LayerRole::ColorTrace;
        if (role == 2) layerRole = core::LayerRole::Correction;
        setPaintLayerRole(layerIndex, layerRole);
    });

    applyToolSettingsToCanvases();

    QToolBar* toolBar = addToolBar(tr("絵コンテ"));
    toolBar->setMovable(false);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &StoryboardWindow::refresh);
    QAction* previewAction = toolBar->addAction(tr("プレビュー"));
    connect(previewAction, &QAction::triggered, this, &StoryboardWindow::openPreview);
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
        refreshLayerPanel();
        return;
    }

    core::Scene& scene = m_project->scene(0);
    auto& panels = scene.storyboard();
    const int count = static_cast<int>(panels.size());
    m_table->setRowCount(count);

    for (int row = 0; row < count; ++row) {
        core::StoryboardPanel& panel = panels[static_cast<size_t>(row)];
        syncStoryboardPanelComposite(panel);

        // No(編集不可)
        auto* noItem = new QTableWidgetItem(QString::number(row + 1));
        noItem->setFlags(noItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColNo, noItem);

        // 絵(サムネ、編集不可)
        auto* thumbItem = new QTableWidgetItem();
        thumbItem->setFlags(thumbItem->flags() & ~Qt::ItemIsEditable);
        thumbItem->setData(Qt::DecorationRole, makeThumbnail(panel));
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
    refreshLayerPanel();
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
    perapera::ui::ensurePaintLayers(panel.layers, panel.activeLayer, panel.drawing, kSheetWidth, kSheetHeight, true);
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
    if (row >= 0) {
        syncSelectedPanelComposite();
        updateThumbnail(row);
    }
    emit edited();
}

void StoryboardWindow::updateThumbnail(int row) {
    if (!m_project || m_project->sceneCount() == 0 || !m_table) return;
    auto& panels = m_project->scene(0).storyboard();
    if (row < 0 || static_cast<size_t>(row) >= panels.size()) return;
    QTableWidgetItem* item = m_table->item(row, kColThumb);
    if (!item) return;

    m_updating = true;
    item->setData(Qt::DecorationRole, makeThumbnail(panels[static_cast<size_t>(row)]));
    m_updating = false;
}

void StoryboardWindow::bindCanvasToSelectedPanel() {
    if (!m_project || m_project->sceneCount() == 0) {
        m_canvas->setBitmap(nullptr);
        m_canvas->setFillBoundaryLayers({});
        m_updating = true;
        m_actionEdit->clear();
        m_dialogueEdit->clear();
        m_updating = false;
        m_actionEdit->setEnabled(false);
        m_dialogueEdit->setEnabled(false);
        refreshLayerPanel();
        return;
    }
    auto& panels = m_project->scene(0).storyboard();
    if (m_selectedRow < 0 || static_cast<size_t>(m_selectedRow) >= panels.size()) {
        m_canvas->setBitmap(nullptr);
        m_canvas->setFillBoundaryLayers({});
        m_updating = true;
        m_actionEdit->clear();
        m_dialogueEdit->clear();
        m_updating = false;
        m_actionEdit->setEnabled(false);
        m_dialogueEdit->setEnabled(false);
        refreshLayerPanel();
        return;
    }

    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    // drawingはコンテ用紙全体(kSheetWidth x kSheetHeight)を覆う1枚の手書きビットマップ。
    // 未確保またはサイズ不一致なら確保し直す(開発中につき旧サイズのデータは破棄してよい)
    syncStoryboardPanelComposite(panel);
    const QSize size = panelCanvasSize(panel);
    m_canvas->setCanvasSize(size.width(), size.height());

    std::vector<GLCanvas::StackEntry> stack;
    std::vector<const core::Bitmap*> boundary;
    for (const core::PaintLayer& layer : panel.layers) {
        if (layer.visible && layer.opacity > 0.0) {
            stack.push_back({&layer.bitmap, QPointF(), layer.opacity});
            if (layer.role == core::LayerRole::ColorTrace) boundary.push_back(&layer.bitmap);
        }
    }
    core::Bitmap* editTarget = panel.layers.empty() ? nullptr : &panel.layers[panel.activeLayer].bitmap;
    m_canvas->setFillBoundaryLayers(std::move(boundary));
    m_canvas->setLayerStack(std::move(stack), editTarget, QPointF());

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

    // 絵の枠を拡大表示中だった場合は、パネル切替後も拡大状態を維持する
    if (m_frameZoomed) m_canvas->zoomToCanvasRect(scaledFrameRect(size.width(), size.height()));
    refreshLayerPanel();
}

void StoryboardWindow::onRadiusSliderChanged(int value) {
    if (m_eraserButton->isChecked()) {
        m_eraserRadius = static_cast<float>(value);
    } else {
        m_penRadius = static_cast<float>(value);
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
    if (!m_canvas) return;
    m_canvas->setPenRadius(m_penRadius);
    m_canvas->setPenColor(m_penColor);
    m_canvas->setEraserRadius(m_eraserRadius);
}

GLCanvas* StoryboardWindow::createCanvas(QWidget* parent) {
    auto* canvas = new GLCanvas(parent);
    canvas->setCanvasSize(kSheetWidth, kSheetHeight);
    if (m_eyedropperButton && m_eyedropperButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Eyedropper);
    } else if (m_fillButton && m_fillButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Fill);
    } else if (m_eraserButton && m_eraserButton->isChecked()) {
        canvas->setTool(GLCanvas::Tool::Eraser);
    } else {
        canvas->setTool(GLCanvas::Tool::Pen);
    }
    canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command> command) {
        m_lastStroke = std::move(command);
        m_lastStrokeTimer.restart();
        onStrokeFinished();
    });
    connect(canvas, &GLCanvas::doubleClickedOnCanvas, this, &StoryboardWindow::onCanvasDoubleClicked);
    connect(canvas, &GLCanvas::colorPicked, this, [this](QColor color) {
        m_penColor = color;
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_penColor.name()));
        applyToolSettingsToCanvases();
        m_penButton->setChecked(true);
    });
    canvas->setPenRadius(m_penRadius);
    canvas->setPenColor(m_penColor);
    canvas->setEraserRadius(m_eraserRadius);
    return canvas;
}

QWidget* StoryboardWindow::createFloatingCanvasPanel(QWidget* parent) {
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
        applyToolSettingsToCanvases();
    });
    connect(colorButton, &QPushButton::clicked, this, [this, colorButton] {
        const QColor chosen = QColorDialog::getColor(m_penColor, this, tr("ペンの色"));
        if (!chosen.isValid()) return;
        m_penColor = chosen;
        const QString style = QStringLiteral("background-color: %1;").arg(m_penColor.name());
        colorButton->setStyleSheet(style);
        if (m_colorButton) m_colorButton->setStyleSheet(style);
        applyToolSettingsToCanvases();
    });
    return panel;
}

void StoryboardWindow::setActiveTool(int tool) {
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

void StoryboardWindow::syncSelectedPanelComposite() {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    syncStoryboardPanelComposite(panels[static_cast<size_t>(m_selectedRow)]);
}

void StoryboardWindow::refreshLayerPanel() {
    if (!m_layerPanel) return;
    QStringList names;
    QList<bool> visible;
    QList<int> opacity;
    int active = -1;

    if (m_project && m_project->sceneCount() > 0 && m_selectedRow >= 0) {
        auto& panels = m_project->scene(0).storyboard();
        if (static_cast<size_t>(m_selectedRow) < panels.size()) {
            core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
            perapera::ui::ensurePaintLayers(panel.layers, panel.activeLayer, panel.drawing, kSheetWidth, kSheetHeight,
                                            true);
            active = static_cast<int>(panel.activeLayer);
            for (const core::PaintLayer& layer : panel.layers) {
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

void StoryboardWindow::addPaintLayer(core::LayerRole role) {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    syncStoryboardPanelComposite(panel);
    const QSize size = panelCanvasSize(panel);

    core::PaintLayer layer;
    layer.role = role;
    layer.name = role == core::LayerRole::ColorTrace ? "塗分け線" : "レイヤー";
    layer.bitmap = core::Bitmap(size.width(), size.height());
    layer.bitmap.fill({0, 0, 0, 0});
    const size_t insertAt = std::min(panel.activeLayer + 1, panel.layers.size());
    panel.layers.insert(panel.layers.begin() + static_cast<ptrdiff_t>(insertAt), std::move(layer));
    panel.activeLayer = insertAt;
    syncStoryboardPanelComposite(panel);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedPanel();
    updateThumbnail(m_selectedRow);
    emit edited();
}

void StoryboardWindow::duplicatePaintLayer(int layerIndex) {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;
    core::PaintLayer copy = panel.layers[static_cast<size_t>(layerIndex)];
    copy.name += " コピー";
    panel.layers.insert(panel.layers.begin() + layerIndex + 1, std::move(copy));
    panel.activeLayer = static_cast<size_t>(layerIndex + 1);
    syncStoryboardPanelComposite(panel);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedPanel();
    updateThumbnail(m_selectedRow);
    emit edited();
}

void StoryboardWindow::removePaintLayer() {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    if (panel.layers.size() <= 1) return;
    panel.layers.erase(panel.layers.begin() + static_cast<ptrdiff_t>(panel.activeLayer));
    panel.activeLayer = std::min(panel.activeLayer, panel.layers.size() - 1);
    syncStoryboardPanelComposite(panel);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedPanel();
    updateThumbnail(m_selectedRow);
    emit edited();
}

void StoryboardWindow::movePaintLayer(int delta) {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    const int from = static_cast<int>(panel.activeLayer);
    const int to = from + delta;
    if (from < 0 || to < 0 || static_cast<size_t>(to) >= panel.layers.size()) return;
    std::swap(panel.layers[static_cast<size_t>(from)], panel.layers[static_cast<size_t>(to)]);
    panel.activeLayer = static_cast<size_t>(to);
    syncStoryboardPanelComposite(panel);
    bindCanvasToSelectedPanel();
    updateThumbnail(m_selectedRow);
    emit edited();
}

void StoryboardWindow::renamePaintLayer(int layerIndex) {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("レイヤー名"), tr("名前:"), QLineEdit::Normal,
                                               QString::fromStdString(panel.layers[static_cast<size_t>(layerIndex)].name),
                                               &ok);
    if (!ok || name.isEmpty()) return;
    panel.layers[static_cast<size_t>(layerIndex)].name = name.toStdString();
    refreshLayerPanel();
    emit edited();
}

void StoryboardWindow::setPaintLayerRole(int layerIndex, core::LayerRole role) {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    if (layerIndex < 0 || static_cast<size_t>(layerIndex) >= panel.layers.size()) return;
    panel.layers[static_cast<size_t>(layerIndex)].role = role;
    bindCanvasToSelectedPanel();
    emit edited();
}

void StoryboardWindow::resizeStoryboardCanvas() {
    if (!m_project || m_project->sceneCount() == 0 || m_selectedRow < 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (static_cast<size_t>(m_selectedRow) >= panels.size()) return;
    core::StoryboardPanel& panel = panels[static_cast<size_t>(m_selectedRow)];
    syncStoryboardPanelComposite(panel);
    const QSize current = panelCanvasSize(panel);

    CanvasSizeDialog dialog(current.width(), current.height(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const int width = dialog.canvasWidth();
    const int height = dialog.canvasHeight();
    if (width == current.width() && height == current.height()) return;

    perapera::ui::resizePaintLayersCentered(panel.layers, width, height);
    panel.drawing = perapera::ui::compositePaintLayers(panel.layers, width, height);
    m_canvas->clearTextureCache();
    bindCanvasToSelectedPanel();
    updateThumbnail(m_selectedRow);
    emit edited();
}

void StoryboardWindow::exportStoryboardPdf() {
    if (!m_project || m_project->sceneCount() == 0) return;
    auto& panels = m_project->scene(0).storyboard();
    if (panels.empty()) return;

    const QString path =
        QFileDialog::getSaveFileName(this, tr("絵コンテを書き出し"), QString(), tr("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    const QString pdfPath = path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive) ? path : path + ".pdf";

    QPdfWriter writer(pdfPath);
    writer.setResolution(300);
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 10, 10, 10),
                                     QPageLayout::Millimeter));
    QPainter painter(&writer);
    if (!painter.isActive()) {
        QMessageBox::warning(this, tr("書き出しエラー"), tr("PDFを書き出せませんでした"));
        return;
    }

    const QRect page = writer.pageLayout().paintRectPixels(writer.resolution());
    constexpr int panelsPerPage = 4;
    const int slotHeight = page.height() / panelsPerPage;
    for (int i = 0; i < static_cast<int>(panels.size()); ++i) {
        if (i > 0 && i % panelsPerPage == 0) writer.newPage();
        core::StoryboardPanel& panel = panels[static_cast<size_t>(i)];
        syncStoryboardPanelComposite(panel);
        const QImage sheet = renderStoryboardSheetImage(panel, i + 1);
        const QRect slot(page.left(), page.top() + (i % panelsPerPage) * slotHeight, page.width(), slotHeight);
        QSize target = sheet.size();
        target.scale(slot.size(), Qt::KeepAspectRatio);
        const QRect dest(slot.left() + (slot.width() - target.width()) / 2,
                         slot.top() + (slot.height() - target.height()) / 2, target.width(), target.height());
        painter.drawImage(dest, sheet);
    }
    painter.end();
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

void StoryboardWindow::onCanvasDoubleClicked(QPointF imagePos) {
    // ダブルクリックの1回目のクリックで既に点が打たれてしまっているため、直近のストロークが
    // ダブルクリック判定時間(+余裕100ms)以内に受領されたものならundoして取り消す
    if (m_lastStroke && m_lastStrokeTimer.isValid() &&
        m_lastStrokeTimer.elapsed() <= QApplication::doubleClickInterval() + 100) {
        if (auto* stroke = dynamic_cast<core::StrokeCommand*>(m_lastStroke.get())) {
            stroke->undo();
            m_canvas->notifyBitmapRegionChanged(stroke->bitmap(), stroke->region());
            const int row = selectedPanelIndex();
            if (row >= 0) updateThumbnail(row);
        }
    }
    m_lastStroke.reset();

    // トグル: 未拡大かつimagePosが絵の枠内ならズームイン。拡大中ならどこをダブルクリックしても解除する。
    // 実際の反映はm_zoomButtonのtoggledハンドラに任せる(ボタンでの切替と経路を一本化するため)
    QRect frameRect = kFrameRect;
    if (m_project && m_project->sceneCount() > 0 && m_selectedRow >= 0) {
        auto& panels = m_project->scene(0).storyboard();
        if (static_cast<size_t>(m_selectedRow) < panels.size()) {
            const QSize size = panelCanvasSize(panels[static_cast<size_t>(m_selectedRow)]);
            frameRect = scaledFrameRect(size.width(), size.height());
        }
    }

    if (m_frameZoomed) {
        m_zoomButton->setChecked(false);
    } else if (frameRect.contains(imagePos.toPoint())) {
        m_zoomButton->setChecked(true);
    }
}

void StoryboardWindow::openPreview() {
    if (m_previewDialog) {
        m_previewDialog->raise();
        m_previewDialog->activateWindow();
        return;
    }
    auto* dialog = new StoryboardPreviewDialog(m_project, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_previewDialog = dialog;
    connect(dialog, &QObject::destroyed, this, [this] { m_previewDialog = nullptr; });
    dialog->show();
    perapera::ui::keepWindowOnScreen(dialog);
}

void StoryboardWindow::detachCanvas() {
    if (m_floatingCanvasWindow || !m_canvas || !m_canvasLayout) return;
    m_canvasLayout->removeWidget(m_canvas);
    m_canvas->deleteLater();
    m_canvas = nullptr;
    auto* window = new FloatingCanvasWindow(tr("絵コンテ キャンバス"), this);
    m_floatingCanvasWindow = window;
    perapera::ui::installRetroWindowFrame(window);
    window->setCentralWidget(createFloatingCanvasPanel(window));
    bindCanvasToSelectedPanel();
    connect(window, &FloatingCanvasWindow::restoreRequested, this, &StoryboardWindow::restoreCanvas);
    connect(window, &QObject::destroyed, this, [this, window] {
        if (m_floatingCanvasWindow == window) m_floatingCanvasWindow = nullptr;
    });
    window->show();
    perapera::ui::keepWindowOnScreen(window);
}

void StoryboardWindow::restoreCanvas() {
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
    bindCanvasToSelectedPanel();
    m_canvas->show();
    m_floatingCanvasWindow = nullptr;
}

void StoryboardWindow::debugZoomToFrame() {
    // 絵の枠のダブルクリックと同じ経路(m_zoomButtonのtoggled)で拡大をONにする
    if (m_zoomButton) m_zoomButton->setChecked(true);
}

void StoryboardWindow::debugOpenPreview() {
    openPreview();
}

void StoryboardWindow::debugDetachCanvas() {
    detachCanvas();
}

FloatingCanvasWindow* StoryboardWindow::debugFloatingCanvasWindow() const {
    return m_floatingCanvasWindow;
}
