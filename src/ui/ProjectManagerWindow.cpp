#include "ProjectManagerWindow.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>

#include "core/Cut.h"
#include "core/Project.h"
#include "core/Scene.h"

namespace {

// 制作進捗7段階(CutStatusのenum順=0..6)の表示名と帯/セルの色
const std::array<QString, 7> kStatusNames = {
    QStringLiteral("未着手"),   QStringLiteral("レイアウト"), QStringLiteral("原画"), QStringLiteral("動画"),
    QStringLiteral("仕上げ"),   QStringLiteral("撮影"),       QStringLiteral("完成")};
const std::array<QString, 7> kStatusColors = {
    QStringLiteral("#9e9e9e"), QStringLiteral("#7e57c2"), QStringLiteral("#42a5f5"), QStringLiteral("#26c6da"),
    QStringLiteral("#66bb6a"), QStringLiteral("#ffa726"), QStringLiteral("#2e7d32")};

enum Column { kColNo = 0, kColName, kColDuration, kColSeconds, kColStatus, kColAction, kColDialogue, kColCount };

}  // namespace

ProjectManagerWindow::ProjectManagerWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("プロジェクト管理"));
    resize(920, 620);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- プロジェクト操作(新規/開く/保存) ---
    auto* projectRow = new QHBoxLayout();
    auto* newProjectButton = new QPushButton(tr("新規プロジェクト…"), central);
    connect(newProjectButton, &QPushButton::clicked, this, [this] { emit newProjectRequested(); });
    projectRow->addWidget(newProjectButton);
    auto* openProjectButton = new QPushButton(tr("プロジェクトを開く…"), central);
    connect(openProjectButton, &QPushButton::clicked, this, [this] { emit openProjectRequested(); });
    projectRow->addWidget(openProjectButton);
    auto* saveProjectButton = new QPushButton(tr("保存"), central);
    connect(saveProjectButton, &QPushButton::clicked, this, [this] { emit saveProjectRequested(); });
    projectRow->addWidget(saveProjectButton);
    projectRow->addStretch(1);
    root->addLayout(projectRow);

    // --- ヘッダー: プロジェクト名 ---
    auto* headerRow = new QHBoxLayout();
    headerRow->addWidget(new QLabel(tr("プロジェクト名:"), central));
    m_projectNameEdit = new QLineEdit(central);
    connect(m_projectNameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_updating || !m_project) return;
        const std::string name = m_projectNameEdit->text().toStdString();
        if (name != m_project->name()) {
            m_project->setName(name);
            emit edited();
        }
    });
    headerRow->addWidget(m_projectNameEdit, 1);
    root->addLayout(headerRow);

    // --- 進捗集計 ---
    auto* summaryBox = new QGroupBox(tr("進捗集計"), central);
    auto* summaryLayout = new QVBoxLayout(summaryBox);
    m_summaryLabel = new QLabel(summaryBox);
    summaryLayout->addWidget(m_summaryLabel);

    m_doneBar = new QProgressBar(summaryBox);
    m_doneBar->setRange(0, 100);
    m_doneBar->setFormat(tr("完成 %p%"));
    summaryLayout->addWidget(m_doneBar);

    // 工程別の割合を積み上げ表示する帯(各セグメントのstretch=そのステータスの本数)
    auto* stageBarWidget = new QWidget(summaryBox);
    stageBarWidget->setMinimumHeight(20);
    m_stageBarLayout = new QHBoxLayout(stageBarWidget);
    m_stageBarLayout->setContentsMargins(0, 0, 0, 0);
    m_stageBarLayout->setSpacing(1);
    for (int i = 0; i < 7; ++i) {
        auto* seg = new QLabel(stageBarWidget);
        seg->setStyleSheet(QStringLiteral("background-color:%1;").arg(kStatusColors[i]));
        m_stageBarLayout->addWidget(seg, 0);
        m_stageSegments[i] = seg;
    }
    summaryLayout->addWidget(stageBarWidget);

    m_stageLegendLabel = new QLabel(summaryBox);
    m_stageLegendLabel->setWordWrap(true);
    summaryLayout->addWidget(m_stageLegendLabel);

    root->addWidget(summaryBox);

    // --- カット構成の操作 ---
    auto* toolRow = new QHBoxLayout();
    m_openButton = new QPushButton(tr("選択カットを開く"), central);
    connect(m_openButton, &QPushButton::clicked, this, &ProjectManagerWindow::activateSelectedCut);
    toolRow->addWidget(m_openButton);
    toolRow->addSpacing(16);
    auto* addButton = new QPushButton(tr("カット追加"), central);
    connect(addButton, &QPushButton::clicked, this, [this] { emit addCutRequested(); });
    toolRow->addWidget(addButton);
    m_removeButton = new QPushButton(tr("カット削除"), central);
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const int row = selectedRow();
        if (row >= 0) emit removeCutRequested(row);
    });
    toolRow->addWidget(m_removeButton);
    m_upButton = new QPushButton(tr("↑"), central);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveSelectedCut(-1); });
    toolRow->addWidget(m_upButton);
    m_downButton = new QPushButton(tr("↓"), central);
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveSelectedCut(1); });
    toolRow->addWidget(m_downButton);
    toolRow->addStretch(1);
    root->addLayout(toolRow);

    // --- 進行管理表 ---
    m_table = new QTableWidget(central);
    m_table->setColumnCount(kColCount);
    m_table->setHorizontalHeaderLabels(
        {tr("No"), tr("カット名"), tr("尺コマ"), tr("秒"), tr("進捗"), tr("内容"), tr("セリフ")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(kColAction, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kColDialogue, QHeaderView::Stretch);
    m_table->setColumnWidth(kColNo, 44);
    m_table->setColumnWidth(kColName, 160);
    m_table->setColumnWidth(kColDuration, 64);
    m_table->setColumnWidth(kColSeconds, 60);
    m_table->setColumnWidth(kColStatus, 100);
    connect(m_table, &QTableWidget::itemChanged, this, &ProjectManagerWindow::onItemChanged);
    // No列のダブルクリックでそのカットを開く(名前/内容などの編集列はダブルクリックで編集に入る)
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == kColNo) activateSelectedCut();
    });
    root->addWidget(m_table, 1);

    setCentralWidget(central);
}

ProjectManagerWindow::~ProjectManagerWindow() {
    // 破棄中に子ウィジェット(QLineEdit等)がeditingFinished等を遅延発火し、既に解放済みの
    // m_project(所有はMainWindow)を触ってクラッシュするのを防ぐため、先にnull化しておく
    m_project = nullptr;
}

void ProjectManagerWindow::setProject(core::Project* project) {
    m_project = project;
    if (m_projectNameEdit) m_projectNameEdit->setText(project ? QString::fromStdString(project->name()) : QString());
}

void ProjectManagerWindow::setFps(int fps) { m_fps = fps > 0 ? fps : 24; }

int ProjectManagerWindow::selectedRow() const { return m_table ? m_table->currentRow() : -1; }

void ProjectManagerWindow::refresh() {
    if (m_projectNameEdit && m_project) {
        const QSignalBlocker b(m_projectNameEdit);
        m_projectNameEdit->setText(QString::fromStdString(m_project->name()));
    }
    rebuildTable();
    updateSummary();
}

void ProjectManagerWindow::rebuildTable() {
    if (!m_table) return;
    m_updating = true;
    m_table->clearContents();

    if (!m_project || m_project->sceneCount() == 0) {
        m_table->setRowCount(0);
        m_updating = false;
        return;
    }

    core::Scene& scene = m_project->scene(0);
    const int rows = static_cast<int>(scene.cutCount());
    m_table->setRowCount(rows);

    for (int row = 0; row < rows; ++row) {
        const core::Cut& cut = scene.cut(static_cast<size_t>(row));

        auto* noItem = new QTableWidgetItem(QString::number(row + 1));
        noItem->setFlags(noItem->flags() & ~Qt::ItemIsEditable);
        noItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, kColNo, noItem);

        m_table->setItem(row, kColName, new QTableWidgetItem(QString::fromStdString(cut.name())));

        auto* durItem = new QTableWidgetItem(QString::number(cut.frameCount()));
        durItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, kColDuration, durItem);

        const double seconds = static_cast<double>(cut.frameCount()) / std::max(1, m_fps);
        auto* secItem = new QTableWidgetItem(QStringLiteral("%1s").arg(seconds, 0, 'f', 2));
        secItem->setFlags(secItem->flags() & ~Qt::ItemIsEditable);
        secItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, kColSeconds, secItem);

        // 進捗コンボ(色付き)
        auto* statusCombo = new QComboBox(m_table);
        for (int s = 0; s < 7; ++s) statusCombo->addItem(kStatusNames[s]);
        const int st = static_cast<int>(cut.status());
        statusCombo->setCurrentIndex(st);
        statusCombo->setStyleSheet(QStringLiteral("QComboBox{background-color:%1;color:white;}").arg(kStatusColors[st]));
        connect(statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row](int index) {
            if (m_updating || !m_project || m_project->sceneCount() == 0) return;
            core::Scene& sc = m_project->scene(0);
            if (static_cast<size_t>(row) >= sc.cutCount()) return;
            sc.cut(static_cast<size_t>(row)).setStatus(static_cast<core::CutStatus>(index));
            if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, kColStatus))) {
                combo->setStyleSheet(
                    QStringLiteral("QComboBox{background-color:%1;color:white;}").arg(kStatusColors[index]));
            }
            updateSummary();
            emit edited();
        });
        m_table->setCellWidget(row, kColStatus, statusCombo);

        m_table->setItem(row, kColAction, new QTableWidgetItem(QString::fromStdString(cut.action())));
        m_table->setItem(row, kColDialogue, new QTableWidgetItem(QString::fromStdString(cut.dialogue())));
    }

    m_updating = false;
}

void ProjectManagerWindow::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || !m_project || m_project->sceneCount() == 0 || !item) return;
    const int row = item->row();
    core::Scene& scene = m_project->scene(0);
    if (static_cast<size_t>(row) >= scene.cutCount()) return;
    core::Cut& cut = scene.cut(static_cast<size_t>(row));

    switch (item->column()) {
        case kColName:
            cut.setName(item->text().toStdString());
            break;
        case kColDuration: {
            bool ok = false;
            const int frames = item->text().toInt(&ok);
            if (!ok || frames < 1) {
                // 不正入力は元の値へ戻す
                const QSignalBlocker b(m_table);
                item->setText(QString::number(cut.frameCount()));
                return;
            }
            cut.setFrameCount(static_cast<size_t>(frames));
            // 秒表示を更新する
            const QSignalBlocker b(m_table);
            const double seconds = static_cast<double>(frames) / std::max(1, m_fps);
            if (auto* secItem = m_table->item(row, kColSeconds))
                secItem->setText(QStringLiteral("%1s").arg(seconds, 0, 'f', 2));
            updateSummary();
            break;
        }
        case kColAction:
            cut.setAction(item->text().toStdString());
            break;
        case kColDialogue:
            cut.setDialogue(item->text().toStdString());
            break;
        default:
            return;  // No/秒/進捗はこの経路では編集しない
    }
    emit edited();
}

void ProjectManagerWindow::moveSelectedCut(int delta) {
    if (!m_project || m_project->sceneCount() == 0) return;
    const int row = selectedRow();
    if (row < 0) return;
    core::Scene& scene = m_project->scene(0);
    const int target = row + delta;
    if (target < 0 || target >= static_cast<int>(scene.cutCount())) return;
    scene.moveCut(static_cast<size_t>(row), static_cast<size_t>(target));
    emit edited();      // 並べ替えはメインウィンドウ側でdirty/カットバー更新を行う
    refresh();          // 表を作り直す
    m_table->selectRow(target);
}

void ProjectManagerWindow::activateSelectedCut() {
    const int row = selectedRow();
    if (row >= 0) emit cutActivated(row);
}

void ProjectManagerWindow::updateSummary() {
    if (!m_summaryLabel) return;
    std::array<int, 7> counts{};
    int total = 0;
    long long totalFrames = 0;
    if (m_project && m_project->sceneCount() > 0) {
        core::Scene& scene = m_project->scene(0);
        total = static_cast<int>(scene.cutCount());
        for (int i = 0; i < total; ++i) {
            const core::Cut& cut = scene.cut(static_cast<size_t>(i));
            const int st = static_cast<int>(cut.status());
            if (st >= 0 && st < 7) ++counts[st];
            totalFrames += static_cast<long long>(cut.frameCount());
        }
    }
    const int done = counts[6];
    const double totalSeconds = static_cast<double>(totalFrames) / std::max(1, m_fps);
    m_summaryLabel->setText(tr("総カット数: %1    総尺: %2 コマ (%3 秒)    完成: %4 / %1")
                                .arg(total)
                                .arg(totalFrames)
                                .arg(totalSeconds, 0, 'f', 2)
                                .arg(done));
    m_doneBar->setValue(total > 0 ? static_cast<int>(std::lround(100.0 * done / total)) : 0);

    // 積み上げ帯: 各工程のstretch=本数、0本のセグメントは隠す
    for (int i = 0; i < 7; ++i) {
        m_stageBarLayout->setStretch(i, counts[i]);
        m_stageSegments[i]->setVisible(counts[i] > 0);
        m_stageSegments[i]->setToolTip(tr("%1: %2本").arg(kStatusNames[i]).arg(counts[i]));
    }
    // 色付きの■で工程別本数の凡例を作る(HTML)
    QString legendHtml;
    for (int i = 0; i < 7; ++i) {
        if (i > 0) legendHtml += QStringLiteral(" &nbsp; ");
        legendHtml += QStringLiteral("<span style='color:%1'>■</span>%2 %3")
                          .arg(kStatusColors[i], kStatusNames[i])
                          .arg(counts[i]);
    }
    m_stageLegendLabel->setText(legendHtml);
}
