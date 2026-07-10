#include "StoryboardWindow.h"

#include <QAction>
#include <QHeaderView>
#include <QImage>
#include <QPixmap>
#include <QTableWidget>
#include <QToolBar>

#include "core/Compositor.h"
#include "core/Project.h"

namespace {
// アプリの作画キャンバスは常にこの解像度(MainWindow.cppのkCanvasWidth/kCanvasHeightと同じ)。
// renderCutFrame()はwidth/height分の拡縮はせず、その解像度で直接合成するため、
// サムネイルは一度フル解像度でレンダリングしてからQImageで縮小する
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
// サムネイル解像度(表示は列幅に合わせて縮小する)
constexpr int kThumbWidth = 384;
constexpr int kThumbHeight = 216;
constexpr int kRowHeight = 120;
constexpr double kFps = 24.0;  // タイムシートは24fps基準

enum Column {
    kColNo = 0,
    kColThumb,
    kColName,
    kColAction,
    kColDialogue,
    kColDuration,
    kColCount,
};

}  // namespace

StoryboardWindow::StoryboardWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("絵コンテ - perapera-anime-maker901"));
    resize(1000, 700);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColCount);
    m_table->setHorizontalHeaderLabels(
        {tr("No"), tr("絵"), tr("カット名"), tr("内容(アクション)"), tr("セリフ"), tr("尺")});
    m_table->verticalHeader()->setVisible(false);  // No列で番号を表示するため行ヘッダは隠す
    m_table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_table->setIconSize(QSize(kThumbWidth / 2, kThumbHeight / 2));
    m_table->setColumnWidth(kColThumb, kThumbWidth / 2 + 16);
    m_table->setColumnWidth(kColName, 140);
    m_table->setColumnWidth(kColAction, 220);
    m_table->setColumnWidth(kColDialogue, 220);
    m_table->setColumnWidth(kColDuration, 140);
    m_table->horizontalHeader()->setStretchLastSection(false);
    setCentralWidget(m_table);

    connect(m_table, &QTableWidget::itemChanged, this, &StoryboardWindow::onItemChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &StoryboardWindow::onCellDoubleClicked);

    QToolBar* toolBar = addToolBar(tr("絵コンテ"));
    toolBar->setMovable(false);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &StoryboardWindow::refresh);
}

void StoryboardWindow::setProject(core::Project* project) {
    m_project = project;
}

void StoryboardWindow::refresh() {
    if (!m_table) return;
    m_updating = true;

    if (!m_project || m_project->sceneCount() == 0) {
        m_table->setRowCount(0);
        m_updating = false;
        return;
    }

    core::Scene& scene = m_project->scene(0);
    const int cutCount = static_cast<int>(scene.cutCount());
    m_table->setRowCount(cutCount);

    for (int row = 0; row < cutCount; ++row) {
        core::Cut& cut = scene.cut(static_cast<size_t>(row));

        // No(編集不可)
        auto* noItem = new QTableWidgetItem(QString::number(row + 1));
        noItem->setFlags(noItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColNo, noItem);

        // サムネイル(編集不可): renderCutFrame()は指定サイズへの拡縮を行わないため、
        // フル解像度(キャンバスと同じ1920x1080)でレンダリングしてからQImageで縮小する
        const core::Bitmap bitmap = core::renderCutFrame(cut, 0, kCanvasWidth, kCanvasHeight);
        const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
        const QPixmap pixmap = QPixmap::fromImage(
            image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        // fromImage()/scaled()は即座にコピー/変換するためbitmap破棄後も安全
        auto* thumbItem = new QTableWidgetItem();
        thumbItem->setFlags(thumbItem->flags() & ~Qt::ItemIsEditable);
        thumbItem->setData(Qt::DecorationRole, pixmap);
        m_table->setItem(row, kColThumb, thumbItem);

        // カット名(編集可)
        m_table->setItem(row, kColName, new QTableWidgetItem(QString::fromStdString(cut.name())));

        // 内容(アクション)(編集可)
        m_table->setItem(row, kColAction, new QTableWidgetItem(QString::fromStdString(cut.action())));

        // セリフ(編集可)
        m_table->setItem(row, kColDialogue, new QTableWidgetItem(QString::fromStdString(cut.dialogue())));

        // 尺(編集不可): Nコマ (S.SS秒)
        const size_t frameCount = cut.frameCount();
        const double seconds = static_cast<double>(frameCount) / kFps;
        auto* durationItem =
            new QTableWidgetItem(tr("%1コマ (%2秒)").arg(frameCount).arg(seconds, 0, 'f', 2));
        durationItem->setFlags(durationItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColDuration, durationItem);
    }

    m_table->resizeRowsToContents();
    for (int row = 0; row < cutCount; ++row) {
        m_table->setRowHeight(row, kRowHeight);
    }

    m_updating = false;
}

void StoryboardWindow::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || !m_project || !item) return;
    if (m_project->sceneCount() == 0) return;
    core::Scene& scene = m_project->scene(0);
    const int row = item->row();
    if (row < 0 || static_cast<size_t>(row) >= scene.cutCount()) return;
    core::Cut& cut = scene.cut(static_cast<size_t>(row));

    switch (item->column()) {
        case kColName:
            cut.setName(item->text().toStdString());
            emit edited();
            emit cutRenamed();
            break;
        case kColAction:
            cut.setAction(item->text().toStdString());
            emit edited();
            break;
        case kColDialogue:
            cut.setDialogue(item->text().toStdString());
            emit edited();
            break;
        default:
            break;  // No/絵/尺は編集不可のため到達しない
    }
}

void StoryboardWindow::onCellDoubleClicked(int row, int column) {
    if (column == kColNo || column == kColThumb) {
        emit cutActivated(row);
    }
}
