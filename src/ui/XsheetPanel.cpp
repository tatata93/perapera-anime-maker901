#include "XsheetPanel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <limits>

#include "ui/ShortcutSettings.h"

namespace {

constexpr int kGridMarkerRole = Qt::UserRole + 1;

class XsheetGridDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);

        const int marker = index.data(kGridMarkerRole).toInt();
        if (marker <= 0) return;

        painter->save();
        const QColor color = marker >= 2 ? QColor(55, 55, 55) : QColor(135, 135, 135);
        painter->setPen(QPen(color, marker >= 2 ? 2.0 : 1.0));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->restore();
    }
};

QToolButton* actionButton(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

QString normalizedClipboardText(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    while (text.endsWith(QLatin1Char('\n'))) text.chop(1);
    return text;
}

}  // namespace

XsheetPanel::XsheetPanel(QWidget* parent) : QDockWidget(tr("タイムシート"), parent) {
    setObjectName(QStringLiteral("XsheetPanel"));

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    auto* statusLayout = new QHBoxLayout();
    statusLayout->setSpacing(4);
    statusLayout->addWidget(new QLabel(tr("現在:"), container));
    m_currentTimeLabel = new QLabel(container);
    m_currentTimeLabel->setMinimumWidth(72);
    statusLayout->addWidget(m_currentTimeLabel);
    statusLayout->addSpacing(8);
    statusLayout->addWidget(new QLabel(tr("総尺:"), container));
    m_frameCountSpin = new QSpinBox(container);
    m_frameCountSpin->setRange(1, 9999);
    m_frameCountSpin->setSuffix(tr(" コマ"));
    m_frameCountSpin->setFocusPolicy(Qt::ClickFocus);
    statusLayout->addWidget(m_frameCountSpin);
    m_durationLabel = new QLabel(container);
    m_durationLabel->setMinimumWidth(118);
    statusLayout->addWidget(m_durationLabel);
    statusLayout->addSpacing(8);
    statusLayout->addWidget(new QLabel(tr("表示:"), container));

    m_viewModeButtons = new QButtonGroup(this);
    m_viewModeButtons->setExclusive(true);
    const QStringList viewLabels = {tr("両方"), tr("原画"), tr("動画")};
    for (int mode = 0; mode < viewLabels.size(); ++mode) {
        auto* button = new QToolButton(container);
        button->setText(viewLabels.at(mode));
        button->setCheckable(true);
        button->setChecked(mode == 0);
        m_viewModeButtons->addButton(button, mode);
        statusLayout->addWidget(button);
    }
    connect(m_viewModeButtons, &QButtonGroup::idClicked, this,
            [this](int id) { setViewMode(static_cast<ViewMode>(id)); });
    statusLayout->addStretch();

    auto* drawingMenu = new QMenu(container);
    QAction* drawingAddAction = drawingMenu->addAction(tr("新しい動画を追加"));
    QAction* drawingDeleteAction = drawingMenu->addAction(tr("現在の動画を削除"));
    auto* drawingButton = new QToolButton(container);
    drawingButton->setText(tr("動画管理"));
    drawingButton->setMenu(drawingMenu);
    drawingButton->setPopupMode(QToolButton::InstantPopup);
    statusLayout->addWidget(drawingButton);

    auto* celMenu = new QMenu(container);
    QAction* celAddAction = celMenu->addAction(tr("セルを追加"));
    QAction* celRenameAction = celMenu->addAction(tr("セル名を変更"));
    QAction* celRemoveAction = celMenu->addAction(tr("セルを削除"));
    celMenu->addSeparator();
    QAction* celMoveBackAction = celMenu->addAction(tr("左（奥）へ移動"));
    QAction* celMoveFrontAction = celMenu->addAction(tr("右（手前）へ移動"));
    auto* celButton = new QToolButton(container);
    celButton->setText(tr("セル管理"));
    celButton->setMenu(celMenu);
    celButton->setPopupMode(QToolButton::InstantPopup);
    statusLayout->addWidget(celButton);

    m_table = new QTableWidget(container);
    m_table->setItemDelegate(new XsheetGridDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::AnyKeyPressed);
    m_table->setTabKeyNavigation(true);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setDefaultSectionSize(23);
    m_table->verticalHeader()->setMinimumSectionSize(18);
    m_table->verticalHeader()->setFixedWidth(48);
    m_table->horizontalHeader()->setMinimumSectionSize(64);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    m_copyAction = new QAction(tr("コピー"), m_table);
    m_cutAction = new QAction(tr("切り取り"), m_table);
    m_pasteAction = new QAction(tr("貼り付け"), m_table);
    m_clearAction = new QAction(tr("消去"), m_table);
    m_holdAction = new QAction(tr("同じ絵を延長"), m_table);
    auto* addKeyShortcutAction = new QAction(tr("原画追加"), m_table);
    auto* addInbetweenShortcutAction = new QAction(tr("中割追加"), m_table);

    perapera::ui::bindShortcut(m_copyAction, perapera::ui::ShortcutScope::Xsheet, QStringLiteral("copy"));
    perapera::ui::bindShortcut(m_cutAction, perapera::ui::ShortcutScope::Xsheet, QStringLiteral("cut"));
    perapera::ui::bindShortcut(m_pasteAction, perapera::ui::ShortcutScope::Xsheet, QStringLiteral("paste"));
    perapera::ui::bindShortcut(m_clearAction, perapera::ui::ShortcutScope::Xsheet, QStringLiteral("clear"));
    perapera::ui::bindShortcut(m_holdAction, perapera::ui::ShortcutScope::Xsheet, QStringLiteral("hold"));
    perapera::ui::bindShortcut(addKeyShortcutAction, perapera::ui::ShortcutScope::Xsheet,
                               QStringLiteral("addKey"));
    perapera::ui::bindShortcut(addInbetweenShortcutAction, perapera::ui::ShortcutScope::Xsheet,
                               QStringLiteral("addInbetween"));
    for (QAction* action : {m_copyAction, m_cutAction, m_pasteAction, m_clearAction, m_holdAction}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_table->addAction(action);
    }
    for (QAction* action : {addKeyShortcutAction, addInbetweenShortcutAction}) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_table->addAction(action);
    }

    m_holdAction->setToolTip(tr("選択範囲を、先頭の動画で埋めます"));
    m_clearAction->setToolTip(tr("選択したACTIONまたはCELLの記入だけを消します"));

    auto* editBar = new QWidget(container);
    auto* editBarLayout = new QHBoxLayout(editBar);
    editBarLayout->setContentsMargins(0, 0, 0, 0);
    editBarLayout->setSpacing(3);

    m_actionControls = new QWidget(editBar);
    auto* actionLayout = new QHBoxLayout(m_actionControls);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(3);
    actionLayout->addWidget(new QLabel(tr("ACTION:"), m_actionControls));
    auto* addKeyButton = new QToolButton(m_actionControls);
    addKeyButton->setText(tr("原画追加"));
    addKeyButton->setToolTip(tr("新しい原画を作り、現在位置へ割り付けます"));
    actionLayout->addWidget(addKeyButton);
    auto* addInbetweenButton = new QToolButton(m_actionControls);
    addInbetweenButton->setText(tr("中割追加"));
    addInbetweenButton->setToolTip(tr("新しい中割を作り、現在位置へ割り付けます"));
    actionLayout->addWidget(addInbetweenButton);
    auto* keyNumberButton = new QToolButton(m_actionControls);
    keyNumberButton->setText(tr("原画番号..."));
    actionLayout->addWidget(keyNumberButton);
    for (const QString& marker : {QStringLiteral("○"), QStringLiteral("●"), QStringLiteral("*")}) {
        auto* markerButton = new QToolButton(m_actionControls);
        markerButton->setText(marker == QStringLiteral("*") ? tr("* 空") : marker + tr(" 中割"));
        connect(markerButton, &QToolButton::clicked, this, [this, marker] { setActionSelection(marker); });
        actionLayout->addWidget(markerButton);
    }
    actionLayout->addWidget(actionButton(m_clearAction, m_actionControls));
    actionLayout->addWidget(actionButton(m_copyAction, m_actionControls));
    actionLayout->addWidget(actionButton(m_cutAction, m_actionControls));
    actionLayout->addWidget(actionButton(m_pasteAction, m_actionControls));
    editBarLayout->addWidget(m_actionControls);

    m_cellControls = new QWidget(editBar);
    auto* cellLayout = new QHBoxLayout(m_cellControls);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(3);
    cellLayout->addWidget(new QLabel(tr("CELL コマ打ち:"), m_cellControls));
    for (int step = 1; step <= 3; ++step) {
        auto* stepButton = new QToolButton(m_cellControls);
        stepButton->setText(tr("%1コマ").arg(step));
        stepButton->setToolTip(tr("選択範囲へ動画1から順番に割り付けます"));
        connect(stepButton, &QToolButton::clicked, this, [this, step] { requestStepPattern(step); });
        cellLayout->addWidget(stepButton);
    }
    cellLayout->addSpacing(8);
    cellLayout->addWidget(actionButton(m_holdAction, m_cellControls));
    auto* blankCellButton = new QToolButton(m_cellControls);
    blankCellButton->setText(tr("空セル"));
    connect(blankCellButton, &QToolButton::clicked, m_clearAction, &QAction::trigger);
    cellLayout->addWidget(blankCellButton);
    cellLayout->addWidget(actionButton(m_copyAction, m_cellControls));
    cellLayout->addWidget(actionButton(m_cutAction, m_cellControls));
    cellLayout->addWidget(actionButton(m_pasteAction, m_cellControls));
    editBarLayout->addWidget(m_cellControls);
    editBarLayout->addStretch();

    layout->addLayout(statusLayout);
    layout->addWidget(editBar);
    layout->addWidget(m_table);
    setWidget(container);

    connect(m_frameCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updating) emit frameCountChanged(value);
    });
    connect(drawingAddAction, &QAction::triggered, this, &XsheetPanel::addDrawingRequested);
    connect(drawingDeleteAction, &QAction::triggered, this, &XsheetPanel::deleteDrawingRequested);
    connect(celAddAction, &QAction::triggered, this, &XsheetPanel::celAddRequested);
    connect(celRemoveAction, &QAction::triggered, this, &XsheetPanel::celRemoveRequested);
    connect(celRenameAction, &QAction::triggered, this, &XsheetPanel::celRenameRequested);
    connect(celMoveBackAction, &QAction::triggered, this, [this] { emit celMoveRequested(-1); });
    connect(celMoveFrontAction, &QAction::triggered, this, [this] { emit celMoveRequested(+1); });

    connect(m_copyAction, &QAction::triggered, this, &XsheetPanel::copySelection);
    connect(m_cutAction, &QAction::triggered, this, &XsheetPanel::cutSelection);
    connect(m_pasteAction, &QAction::triggered, this, &XsheetPanel::pasteSelection);
    connect(m_clearAction, &QAction::triggered, this, &XsheetPanel::clearSelection);
    connect(m_holdAction, &QAction::triggered, this, &XsheetPanel::fillHoldSelection);
    connect(addKeyButton, &QToolButton::clicked, addKeyShortcutAction, &QAction::trigger);
    connect(addInbetweenButton, &QToolButton::clicked, addInbetweenShortcutAction, &QAction::trigger);
    connect(addKeyShortcutAction, &QAction::triggered, this, &XsheetPanel::addKeyDrawingRequested);
    connect(addInbetweenShortcutAction, &QAction::triggered, this,
            &XsheetPanel::addInbetweenDrawingRequested);
    connect(keyNumberButton, &QToolButton::clicked, this, &XsheetPanel::promptKeyNumber);

    connect(m_table, &QTableWidget::itemChanged, this, &XsheetPanel::onItemChanged);
    connect(m_table, &QTableWidget::cellClicked, this, &XsheetPanel::onCellClicked);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &XsheetPanel::showCellContextMenu);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &XsheetPanel::updateActionStates);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &XsheetPanel::showHeaderContextMenu);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int column) {
        const int cel = colToCel(column);
        if (cel < 0) return;
        emit cellClicked(cel, m_currentFrame);
        emit celRenameRequested();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        const bool focused = now && (now == m_table || m_table->isAncestorOf(now));
        emit tableFocusChanged(focused);
    });

    updateActionStates();
    updateEditControls();
}

void XsheetPanel::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || !item) return;
    const int celIndex = colToCel(item->column());
    if (celIndex < 0) return;

    const QString text = item->text().trimmed();
    if (isActionColumn(item->column())) {
        if (text.size() > 12) {
            setSheet(m_celNames, m_celVisible, m_exposures, m_actionTracks, m_frameCount,
                     m_currentFrame, m_activeCel, m_fps);
            return;
        }
        emitEdits({}, {{celIndex, item->row(), text}});
        return;
    }

    int drawing = -1;
    if (text == QStringLiteral("│")) {
        setSheet(m_celNames, m_celVisible, m_exposures, m_actionTracks, m_frameCount,
                 m_currentFrame, m_activeCel, m_fps);
        return;
    }
    if (!text.isEmpty() && text != QStringLiteral("*") && text != QStringLiteral("-") &&
        text.compare(QStringLiteral("x"), Qt::CaseInsensitive) != 0 && text != QStringLiteral("×")) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (!ok || value <= 0) {
            setSheet(m_celNames, m_celVisible, m_exposures, m_actionTracks, m_frameCount,
                     m_currentFrame, m_activeCel, m_fps);
            return;
        }
        drawing = value - 1;
    }
    emitEdits({{celIndex, item->row(), drawing}});
}

void XsheetPanel::onCellClicked(int row, int column) {
    if (row < 0) return;
    const int cel = colToCel(column);
    emit cellClicked(cel >= 0 ? cel : m_activeCel, row);
    updateEditControls();
}

void XsheetPanel::showHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = m_table->horizontalHeader();
    const int column = header->logicalIndexAt(pos);
    const int cel = colToCel(column);
    if (cel < 0) return;

    emit cellClicked(cel, m_currentFrame);

    QMenu menu(this);
    QAction* renameAction = menu.addAction(tr("セル名を変更"));
    QAction* visibilityAction =
        menu.addAction(cel < m_celVisible.size() && m_celVisible.at(cel) ? tr("セルを非表示") : tr("セルを表示"));
    menu.addSeparator();
    QAction* moveBackAction = menu.addAction(tr("左（奥）へ移動"));
    QAction* moveFrontAction = menu.addAction(tr("右（手前）へ移動"));
    menu.addSeparator();
    QAction* removeAction = menu.addAction(tr("セルを削除"));

    QAction* chosen = menu.exec(header->viewport()->mapToGlobal(pos));
    if (chosen == renameAction) {
        emit celRenameRequested();
    } else if (chosen == visibilityAction) {
        emit celVisibilityToggleRequested(cel);
    } else if (chosen == moveBackAction) {
        emit celMoveRequested(-1);
    } else if (chosen == moveFrontAction) {
        emit celMoveRequested(+1);
    } else if (chosen == removeAction) {
        emit celRemoveRequested();
    }
}

void XsheetPanel::showCellContextMenu(const QPoint& pos) {
    const QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid() || colToCel(index.column()) < 0) return;

    if (colToCel(index.column()) >= 0 && !m_table->selectionModel()->isSelected(index)) {
        m_table->setCurrentCell(index.row(), index.column(), QItemSelectionModel::ClearAndSelect);
    }
    onCellClicked(index.row(), index.column());

    QMenu menu(this);
    menu.addAction(m_copyAction);
    menu.addAction(m_cutAction);
    menu.addAction(m_pasteAction);
    menu.addSeparator();
    if (isActionColumn(index.column())) {
        QAction* keyNumberAction = menu.addAction(tr("原画番号..."));
        QAction* circleAction = menu.addAction(tr("○ 中割"));
        QAction* dotAction = menu.addAction(tr("● 中割"));
        QAction* blankAction = menu.addAction(tr("* 空"));
        menu.addAction(m_clearAction);
        menu.addSeparator();
        QAction* addKeyAction = menu.addAction(tr("原画を追加"));
        QAction* addInbetweenAction = menu.addAction(tr("中割を追加"));
        connect(keyNumberAction, &QAction::triggered, this, &XsheetPanel::promptKeyNumber);
        connect(circleAction, &QAction::triggered, this,
                [this] { setActionSelection(QStringLiteral("○")); });
        connect(dotAction, &QAction::triggered, this,
                [this] { setActionSelection(QStringLiteral("●")); });
        connect(blankAction, &QAction::triggered, this,
                [this] { setActionSelection(QStringLiteral("*")); });
        connect(addKeyAction, &QAction::triggered, this, &XsheetPanel::addKeyDrawingRequested);
        connect(addInbetweenAction, &QAction::triggered, this,
                &XsheetPanel::addInbetweenDrawingRequested);
    } else {
        menu.addAction(m_holdAction);
        menu.addAction(m_clearAction);
        QMenu* stepMenu = menu.addMenu(tr("コマ打ち"));
        for (int step = 1; step <= 3; ++step) {
            QAction* action = stepMenu->addAction(tr("%1コマ打ち").arg(step));
            connect(action, &QAction::triggered, this, [this, step] { requestStepPattern(step); });
        }
        menu.addSeparator();
        QAction* addDrawingAction = menu.addAction(tr("新しい動画を追加"));
        connect(addDrawingAction, &QAction::triggered, this, &XsheetPanel::addDrawingRequested);
    }
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void XsheetPanel::copySelection() {
    const QModelIndexList selected = m_table->selectionModel()->selectedIndexes();
    int minRow = std::numeric_limits<int>::max();
    int maxRow = -1;
    int minCol = std::numeric_limits<int>::max();
    int maxCol = -1;
    for (const QModelIndex& index : selected) {
        if (colToCel(index.column()) < 0) continue;
        minRow = std::min(minRow, index.row());
        maxRow = std::max(maxRow, index.row());
        minCol = std::min(minCol, index.column());
        maxCol = std::max(maxCol, index.column());
    }
    if (maxRow < 0 || maxCol < 0) return;

    QStringList rows;
    for (int row = minRow; row <= maxRow; ++row) {
        QStringList values;
        for (int col = minCol; col <= maxCol; ++col) {
            const int cel = colToCel(col);
            if (isActionColumn(col)) {
                values.append(cel >= 0 && cel < m_actionTracks.size() &&
                                      row < m_actionTracks.at(cel).size()
                                  ? m_actionTracks.at(cel).at(row)
                                  : QString());
            } else {
                const int drawing =
                    cel >= 0 && cel < m_exposures.size() && row < m_exposures.at(cel).size()
                        ? m_exposures.at(cel).at(row)
                        : -1;
                values.append(drawing >= 0 ? QString::number(drawing + 1) : QString());
            }
        }
        rows.append(values.join(QLatin1Char('\t')));
    }
    QApplication::clipboard()->setText(rows.join(QLatin1Char('\n')));
}

void XsheetPanel::cutSelection() {
    copySelection();
    clearSelection();
}

void XsheetPanel::pasteSelection() {
    const QString text = normalizedClipboardText(QApplication::clipboard()->text());
    if (text.isEmpty()) return;

    QModelIndex start = m_table->currentIndex();
    if (!start.isValid() || colToCel(start.column()) < 0) {
        start = m_table->model()->index(std::clamp(m_currentFrame, 0, m_frameCount - 1),
                                        celToPrimaryCol(m_activeCel));
    }
    if (!start.isValid()) return;

    const QStringList rows = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const QStringList singleValues = rows.size() == 1 ? rows.first().split(QLatin1Char('\t'), Qt::KeepEmptyParts)
                                                      : QStringList();
    std::vector<PendingExposureEdit> exposureEdits;
    std::vector<PendingActionEdit> actionEdits;

    if (rows.size() == 1 && singleValues.size() == 1 && selectedEditableCount() > 1) {
        const QString valueText = singleValues.first().trimmed();
        for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
            const int cel = colToCel(index.column());
            if (cel < 0) continue;
            if (isActionColumn(index.column())) {
                actionEdits.push_back({cel, index.row(), valueText.left(12)});
            } else {
                bool ok = false;
                const int value = valueText.toInt(&ok);
                const int drawing = valueText.isEmpty() ? -1 : (ok && value > 0 ? value - 1 : -2);
                if (drawing >= -1) exposureEdits.push_back({cel, index.row(), drawing});
            }
        }
    } else {
        for (int rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
            const QStringList values = rows.at(rowOffset).split(QLatin1Char('\t'), Qt::KeepEmptyParts);
            for (int colOffset = 0; colOffset < values.size(); ++colOffset) {
                const int row = start.row() + rowOffset;
                const int col = start.column() + colOffset;
                const int cel = colToCel(col);
                if (row >= m_frameCount || cel < 0 || cel >= m_exposures.size()) continue;
                const QString valueText = values.at(colOffset).trimmed();
                if (isActionColumn(col)) {
                    actionEdits.push_back({cel, row, valueText.left(12)});
                    continue;
                }
                bool ok = false;
                const int value = valueText.toInt(&ok);
                if (valueText.isEmpty() || valueText == QStringLiteral("*") || valueText == QStringLiteral("-") ||
                    valueText.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0 ||
                    valueText == QStringLiteral("×")) {
                    exposureEdits.push_back({cel, row, -1});
                } else if (ok && value > 0) {
                    exposureEdits.push_back({cel, row, value - 1});
                }
            }
        }
    }
    emitEdits(exposureEdits, actionEdits);
}

void XsheetPanel::clearSelection() {
    std::vector<PendingExposureEdit> exposureEdits;
    std::vector<PendingActionEdit> actionEdits;
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        const int cel = colToCel(index.column());
        if (cel < 0) continue;
        if (isActionColumn(index.column())) {
            actionEdits.push_back({cel, index.row(), QString()});
        } else {
            exposureEdits.push_back({cel, index.row(), -1});
        }
    }
    if (exposureEdits.empty() && actionEdits.empty() && m_table->currentIndex().isValid()) {
        const QModelIndex index = m_table->currentIndex();
        const int cel = colToCel(index.column());
        if (cel >= 0 && isActionColumn(index.column())) {
            actionEdits.push_back({cel, index.row(), QString()});
        } else if (cel >= 0) {
            exposureEdits.push_back({cel, index.row(), -1});
        }
    }
    emitEdits(exposureEdits, actionEdits);
}

void XsheetPanel::fillHoldSelection() {
    QMap<int, QList<int>> rowsByCel;
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        const int cel = colToCel(index.column());
        if (cel >= 0 && !isActionColumn(index.column())) rowsByCel[cel].append(index.row());
    }

    std::vector<PendingExposureEdit> edits;
    for (auto it = rowsByCel.begin(); it != rowsByCel.end(); ++it) {
        QList<int> rows = it.value();
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        if (rows.size() < 2) continue;

        const int cel = it.key();
        int sourceRow = rows.first();
        int drawing = sourceRow < m_exposures.at(cel).size() ? m_exposures.at(cel).at(sourceRow) : -1;
        while (drawing < 0 && sourceRow > 0) {
            --sourceRow;
            drawing = m_exposures.at(cel).at(sourceRow);
        }
        if (drawing < 0) continue;
        for (int row : rows) edits.push_back({cel, row, drawing});
    }
    emitEdits(edits);
}

void XsheetPanel::requestStepPattern(int step) {
    int first = m_currentFrame;
    int last = m_frameCount - 1;
    if (selectedFrameRange(first, last) && first == last) last = m_frameCount - 1;
    emit stepPatternRequested(step, first, last);
}

void XsheetPanel::setActionSelection(const QString& entry) {
    std::vector<PendingActionEdit> edits;
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        const int cel = colToCel(index.column());
        if (cel >= 0 && isActionColumn(index.column())) {
            edits.push_back({cel, index.row(), entry});
        }
    }
    if (edits.empty()) {
        const QModelIndex current = m_table->currentIndex();
        if (current.isValid() && isActionColumn(current.column())) {
            edits.push_back({colToCel(current.column()), current.row(), entry});
        }
    }
    emitEdits({}, edits);
}

void XsheetPanel::promptKeyNumber() {
    bool ok = false;
    const QString value = QInputDialog::getText(
        this, tr("原画番号"), tr("ACTIONに記入する原画番号:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    const QString trimmed = value.trimmed();
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9]{1,12}$"));
    if (!pattern.match(trimmed).hasMatch()) {
        QMessageBox::information(this, tr("原画番号"),
                                 tr("半角英数字で1～12文字にしてください。"));
        return;
    }
    setActionSelection(trimmed);
}

void XsheetPanel::setViewMode(ViewMode mode) {
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    setSheet(m_celNames, m_celVisible, m_exposures, m_actionTracks, m_frameCount,
             m_currentFrame, m_activeCel, m_fps);
    updateEditControls();
}

void XsheetPanel::emitEdits(const std::vector<PendingExposureEdit>& exposureEdits,
                            const std::vector<PendingActionEdit>& actionEdits) {
    QList<int> exposureCels;
    QList<int> exposureFrames;
    QList<int> drawings;
    QSet<QString> seenExposure;
    for (const PendingExposureEdit& edit : exposureEdits) {
        if (edit.cel < 0 || edit.cel >= m_exposures.size() || edit.frame < 0 || edit.frame >= m_frameCount) continue;
        const QString key = QStringLiteral("%1/%2").arg(edit.cel).arg(edit.frame);
        if (seenExposure.contains(key)) continue;
        seenExposure.insert(key);
        exposureCels.append(edit.cel);
        exposureFrames.append(edit.frame);
        drawings.append(edit.drawing);
    }

    QList<int> actionCels;
    QList<int> actionFrames;
    QStringList actionEntries;
    QSet<QString> seenAction;
    for (const PendingActionEdit& edit : actionEdits) {
        if (edit.cel < 0 || edit.cel >= m_actionTracks.size() || edit.frame < 0 ||
            edit.frame >= m_frameCount) {
            continue;
        }
        const QString key = QStringLiteral("%1/%2").arg(edit.cel).arg(edit.frame);
        if (seenAction.contains(key)) continue;
        seenAction.insert(key);
        actionCels.append(edit.cel);
        actionFrames.append(edit.frame);
        actionEntries.append(edit.entry.trimmed());
    }

    if (!exposureCels.isEmpty() || !actionCels.isEmpty()) {
        emit sheetEditsRequested(exposureCels, exposureFrames, drawings, actionCels,
                                 actionFrames, actionEntries);
    }
}

void XsheetPanel::updateActionStates() {
    const int count = selectedEditableCount();
    const int cellCount = selectedCellCount();
    if (m_copyAction) m_copyAction->setEnabled(count > 0);
    if (m_cutAction) m_cutAction->setEnabled(count > 0);
    if (m_clearAction) m_clearAction->setEnabled(count > 0);
    if (m_holdAction) m_holdAction->setEnabled(cellCount > 1);
    if (m_pasteAction) {
        m_pasteAction->setEnabled(m_table && m_table->currentIndex().isValid() &&
                                  colToCel(m_table->currentIndex().column()) >= 0);
    }
    updateEditControls();
}

void XsheetPanel::updateEditControls() {
    if (!m_actionControls || !m_cellControls) return;
    bool showAction = m_viewMode == ViewMode::Action;
    if (m_viewMode == ViewMode::Both && m_table && m_table->currentIndex().isValid()) {
        showAction = isActionColumn(m_table->currentIndex().column());
    }
    m_actionControls->setVisible(showAction);
    m_cellControls->setVisible(!showAction);
}

int XsheetPanel::selectedEditableCount() const {
    if (!m_table || !m_table->selectionModel()) return 0;
    int count = 0;
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        if (colToCel(index.column()) >= 0) ++count;
    }
    return count;
}

int XsheetPanel::selectedCellCount() const {
    if (!m_table || !m_table->selectionModel()) return 0;
    int count = 0;
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        if (colToCel(index.column()) >= 0 && !isActionColumn(index.column())) ++count;
    }
    return count;
}

bool XsheetPanel::selectedFrameRange(int& firstFrame, int& lastFrame) const {
    int first = std::numeric_limits<int>::max();
    int last = -1;
    const int activeColumn = celToCellCol(m_activeCel);
    for (const QModelIndex& index : m_table->selectionModel()->selectedIndexes()) {
        if (index.column() != activeColumn) continue;
        first = std::min(first, index.row());
        last = std::max(last, index.row());
    }
    if (last < 0) return false;
    firstFrame = first;
    lastFrame = last;
    return true;
}

void XsheetPanel::updateRowBackgrounds(int frame) {
    if (!m_table || frame < 0 || frame >= m_frameCount) return;
    const int block = m_fps == 24 ? frame / 6 : frame / std::max(1, m_fps / 4);

    if (QTableWidgetItem* timingItem = m_table->item(frame, kTimingColumn)) {
        timingItem->setBackground(frame == m_currentFrame ? QColor(255, 222, 218) : QColor(226, 226, 226));
    }
    for (int cel = 0; cel < m_celNames.size(); ++cel) {
        for (int col : {celToActionCol(cel), celToCellCol(cel)}) {
            if (col < 0) continue;
            QTableWidgetItem* item = m_table->item(frame, col);
            if (!item) continue;
            QColor background = isActionColumn(col)
                                    ? ((block % 2 == 0) ? QColor(255, 250, 232)
                                                        : QColor(250, 244, 220))
                                    : ((block % 2 == 0) ? QColor(255, 255, 255)
                                                        : QColor(247, 247, 247));
            if (cel == m_activeCel) {
                background = isActionColumn(col)
                                 ? ((block % 2 == 0) ? QColor(250, 238, 196)
                                                     : QColor(244, 230, 184))
                                 : ((block % 2 == 0) ? QColor(232, 242, 253)
                                                     : QColor(224, 236, 249));
            }
            if (frame == m_currentFrame) background = QColor(255, 238, 222);
            item->setBackground(background);
        }
    }
}

int XsheetPanel::colToCel(int col) const {
    if (col <= kTimingColumn) return -1;
    return m_viewMode == ViewMode::Both ? (col - 1) / 2 : col - 1;
}

bool XsheetPanel::isActionColumn(int col) const {
    if (col <= kTimingColumn) return false;
    if (m_viewMode == ViewMode::Action) return true;
    if (m_viewMode == ViewMode::Cell) return false;
    return (col - 1) % 2 == 0;
}

int XsheetPanel::celToActionCol(int celIndex) const {
    if (m_viewMode == ViewMode::Cell) return -1;
    return m_viewMode == ViewMode::Both ? 1 + celIndex * 2 : 1 + celIndex;
}

int XsheetPanel::celToCellCol(int celIndex) const {
    if (m_viewMode == ViewMode::Action) return -1;
    return m_viewMode == ViewMode::Both ? 2 + celIndex * 2 : 1 + celIndex;
}

int XsheetPanel::celToPrimaryCol(int celIndex) const {
    return m_viewMode == ViewMode::Action ? celToActionCol(celIndex) : celToCellCol(celIndex);
}

int XsheetPanel::sheetColumnCount(int celCount) const {
    return 1 + (m_viewMode == ViewMode::Both ? celCount * 2 : celCount);
}

QString XsheetPanel::timeLabel(int zeroBasedFrame) const {
    const int oneBased = std::max(0, zeroBasedFrame) + 1;
    const int seconds = oneBased / std::max(1, m_fps);
    const int frameInSecond = oneBased % std::max(1, m_fps);
    return QStringLiteral("%1+%2").arg(seconds).arg(frameInSecond, 2, 10, QLatin1Char('0'));
}

void XsheetPanel::debugSelectExposureRange(int celIndex, int firstFrame, int lastFrame) {
    if (!m_table || celIndex < 0 || celIndex >= m_exposures.size()) return;
    firstFrame = std::clamp(firstFrame, 0, m_frameCount - 1);
    lastFrame = std::clamp(lastFrame, firstFrame, m_frameCount - 1);
    const int column = celToCellCol(celIndex);
    if (column < 0) return;
    QItemSelection selection(m_table->model()->index(firstFrame, column),
                             m_table->model()->index(lastFrame, column));
    m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    m_table->selectionModel()->setCurrentIndex(m_table->model()->index(firstFrame, column),
                                               QItemSelectionModel::NoUpdate);
}

void XsheetPanel::debugSelectActionCell(int celIndex, int frame) {
    if (!m_table || celIndex < 0 || celIndex >= m_actionTracks.size()) return;
    frame = std::clamp(frame, 0, m_frameCount - 1);
    const int column = celToActionCol(celIndex);
    if (column < 0) return;
    m_table->setCurrentCell(frame, column, QItemSelectionModel::ClearAndSelect);
}

void XsheetPanel::setSheet(const QStringList& celNames, const QList<bool>& celVisible,
                           const QList<QList<int>>& exposures,
                           const QList<QStringList>& actionTracks, int frameCount, int currentFrame,
                           int activeCel, int fps) {
    const QModelIndexList oldSelection =
        m_table->selectionModel() ? m_table->selectionModel()->selectedIndexes() : QModelIndexList();
    const QModelIndex oldCurrent = m_table->currentIndex();
    QWidget* focus = QApplication::focusWidget();
    const bool preserveSelection = focus && (focus == m_table || m_table->isAncestorOf(focus));
    const int normalizedFrameCount = std::max(1, frameCount);
    const int normalizedCurrentFrame = std::clamp(currentFrame, 0, normalizedFrameCount - 1);
    const int normalizedActiveCel =
        std::clamp(activeCel, 0, std::max(0, static_cast<int>(celNames.size()) - 1));
    const int normalizedFps = std::max(1, fps);
    const int previousCurrentFrame = m_currentFrame;
    const bool positionOnly =
        m_table->rowCount() == normalizedFrameCount &&
        m_table->columnCount() == sheetColumnCount(static_cast<int>(celNames.size())) &&
        m_celNames == celNames && m_celVisible == celVisible && m_exposures == exposures &&
        m_actionTracks == actionTracks && m_frameCount == normalizedFrameCount &&
        m_activeCel == normalizedActiveCel && m_fps == normalizedFps;

    if (positionOnly) {
        m_updating = true;
        m_currentFrame = normalizedCurrentFrame;
        m_currentTimeLabel->setText(
            tr("%1（%2コマ目）").arg(timeLabel(m_currentFrame)).arg(m_currentFrame + 1));
        updateRowBackgrounds(previousCurrentFrame);
        updateRowBackgrounds(m_currentFrame);

        if (!celNames.isEmpty()) {
            const QModelIndex target =
                m_table->model()->index(m_currentFrame, celToPrimaryCol(m_activeCel));
            if (!preserveSelection) {
                m_table->setCurrentCell(target.row(), target.column(), QItemSelectionModel::ClearAndSelect);
            }
            m_table->scrollTo(target, QAbstractItemView::PositionAtCenter);
        }
        m_updating = false;
        updateActionStates();
        return;
    }

    m_updating = true;
    m_celNames = celNames;
    m_celVisible = celVisible;
    m_exposures = exposures;
    m_actionTracks = actionTracks;
    m_frameCount = normalizedFrameCount;
    m_currentFrame = normalizedCurrentFrame;
    m_activeCel = normalizedActiveCel;
    m_fps = normalizedFps;

    m_frameCountSpin->setValue(m_frameCount);
    m_currentTimeLabel->setText(tr("%1（%2コマ目）").arg(timeLabel(m_currentFrame)).arg(m_currentFrame + 1));
    m_durationLabel->setText(tr("%1 / %2fps").arg(timeLabel(m_frameCount - 1)).arg(m_fps));

    const int columnCount = sheetColumnCount(static_cast<int>(celNames.size()));
    if (m_table->rowCount() != m_frameCount || m_table->columnCount() != columnCount) {
        m_table->clear();
        m_table->setRowCount(m_frameCount);
        m_table->setColumnCount(columnCount);
        for (int row = 0; row < m_frameCount; ++row) {
            for (int col = 0; col < columnCount; ++col) {
                m_table->setItem(row, col, new QTableWidgetItem());
            }
        }
    }

    auto* timingHeader = new QTableWidgetItem(tr("秒+コマ"));
    QFont timingHeaderFont = timingHeader->font();
    timingHeaderFont.setBold(true);
    timingHeader->setFont(timingHeaderFont);
    m_table->setHorizontalHeaderItem(kTimingColumn, timingHeader);
    m_table->horizontalHeader()->setSectionResizeMode(kTimingColumn, QHeaderView::Fixed);
    m_table->setColumnWidth(kTimingColumn, 72);

    for (int cel = 0; cel < celNames.size(); ++cel) {
        const bool visible = cel < celVisible.size() ? celVisible.at(cel) : true;
        for (int col : {celToActionCol(cel), celToCellCol(cel)}) {
            if (col < 0) continue;
            QString label =
                tr("%1 %2").arg(celNames.at(cel), isActionColumn(col) ? tr("ACTION") : tr("CELL"));
            if (!visible) label += tr(" [非表示]");
            auto* headerItem = new QTableWidgetItem(label);
            QFont font = headerItem->font();
            font.setBold(cel == m_activeCel);
            headerItem->setFont(font);
            if (cel == m_activeCel) {
                headerItem->setBackground(isActionColumn(col) ? QColor(239, 220, 158)
                                                               : QColor(202, 222, 246));
            }
            m_table->setHorizontalHeaderItem(col, headerItem);
            m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Interactive);
            m_table->setColumnWidth(col, isActionColumn(col) ? 84 : 82);
        }
    }
    m_table->horizontalHeader()->setStretchLastSection(false);

    QStringList rowLabels;
    rowLabels.reserve(m_frameCount);
    for (int frame = 0; frame < m_frameCount; ++frame) rowLabels.append(QString::number(frame + 1));
    m_table->setVerticalHeaderLabels(rowLabels);

    for (int frame = 0; frame < m_frameCount; ++frame) {
        const int oneBased = frame + 1;
        const bool secondBoundary = oneBased % m_fps == 0;
        const bool quarterBoundary = m_fps == 24 && oneBased % 6 == 0;
        const int marker = secondBoundary ? 2 : (quarterBoundary ? 1 : 0);
        const int block = m_fps == 24 ? frame / 6 : frame / std::max(1, m_fps / 4);

        QTableWidgetItem* timingItem = m_table->item(frame, kTimingColumn);
        timingItem->setText(timeLabel(frame));
        timingItem->setTextAlignment(Qt::AlignCenter);
        timingItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        timingItem->setData(kGridMarkerRole, marker);
        QFont timingFont = timingItem->font();
        timingFont.setBold(frame == 0 || frame % m_fps == 0);
        timingItem->setFont(timingFont);
        timingItem->setBackground(frame == m_currentFrame ? QColor(255, 222, 218)
                                                          : QColor(226, 226, 226));

        for (int cel = 0; cel < celNames.size(); ++cel) {
            const int actionCol = celToActionCol(cel);
            if (actionCol >= 0) {
                QTableWidgetItem* actionItem = m_table->item(frame, actionCol);
                const QStringList* actionColumn =
                    cel < actionTracks.size() ? &actionTracks.at(cel) : nullptr;
                const QString entry =
                    actionColumn && frame < actionColumn->size() ? actionColumn->at(frame) : QString();
                const bool markerEntry = entry == QStringLiteral("○") || entry == QStringLiteral("●") ||
                                         entry == QStringLiteral("*");

                actionItem->setData(kGridMarkerRole, marker);
                actionItem->setText(entry);
                actionItem->setTextAlignment(Qt::AlignCenter);
                actionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
                QFont actionFont = actionItem->font();
                actionFont.setBold(!entry.isEmpty() && !markerEntry);
                actionItem->setFont(actionFont);
                actionItem->setForeground(entry == QStringLiteral("*") ? QColor(115, 115, 115)
                                                                        : QColor(Qt::black));

                QColor actionBackground =
                    (block % 2 == 0) ? QColor(255, 250, 232) : QColor(250, 244, 220);
                if (cel == m_activeCel) {
                    actionBackground =
                        (block % 2 == 0) ? QColor(250, 238, 196) : QColor(244, 230, 184);
                }
                if (frame == m_currentFrame) actionBackground = QColor(255, 238, 222);
                actionItem->setBackground(actionBackground);
                actionItem->setToolTip(
                    tr("%1 ACTION / %2 / 原画番号・○中割・●中割・*空")
                        .arg(celNames.at(cel), timeLabel(frame)));
            }

            const int cellCol = celToCellCol(cel);
            if (cellCol < 0) continue;
            QTableWidgetItem* item = m_table->item(frame, cellCol);
            const QList<int>* exposureColumn = cel < exposures.size() ? &exposures.at(cel) : nullptr;
            const int drawing = exposureColumn && frame < exposureColumn->size() ? exposureColumn->at(frame) : -1;
            const int previous = exposureColumn && frame > 0 && frame - 1 < exposureColumn->size()
                                     ? exposureColumn->at(frame - 1)
                                     : -1;
            const bool held = drawing >= 0 && drawing == previous;

            item->setData(Qt::UserRole, drawing);
            item->setData(kGridMarkerRole, marker);
            item->setText(drawing < 0 ? QString() : (held ? QStringLiteral("│") : QString::number(drawing + 1)));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            QFont font = item->font();
            font.setBold(drawing >= 0 && !held);
            item->setFont(font);
            item->setForeground(held ? QColor(105, 105, 105) : QColor(Qt::black));

            QColor background = (block % 2 == 0) ? QColor(255, 255, 255) : QColor(247, 247, 247);
            if (cel == m_activeCel) background = (block % 2 == 0) ? QColor(232, 242, 253) : QColor(224, 236, 249);
            if (frame == m_currentFrame) background = QColor(255, 238, 222);
            item->setBackground(background);

            const QString state =
                drawing < 0 ? tr("空セル")
                            : (held ? tr("動画%1（継続）").arg(drawing + 1) : tr("動画%1").arg(drawing + 1));
            item->setToolTip(tr("%1セル / %2 / %3").arg(celNames.at(cel), timeLabel(frame), state));
        }
    }

    if (!celNames.isEmpty()) {
        const QModelIndex target =
            m_table->model()->index(m_currentFrame, celToPrimaryCol(m_activeCel));
        if (preserveSelection && !oldSelection.isEmpty()) {
            m_table->selectionModel()->clearSelection();
            for (const QModelIndex& index : oldSelection) {
                if (index.row() < m_frameCount && index.column() < columnCount) {
                    m_table->selectionModel()->select(m_table->model()->index(index.row(), index.column()),
                                                      QItemSelectionModel::Select);
                }
            }
            if (oldCurrent.isValid() && oldCurrent.row() < m_frameCount && oldCurrent.column() < columnCount) {
                m_table->selectionModel()->setCurrentIndex(
                    m_table->model()->index(oldCurrent.row(), oldCurrent.column()), QItemSelectionModel::NoUpdate);
            }
        } else {
            m_table->setCurrentCell(target.row(), target.column(), QItemSelectionModel::ClearAndSelect);
        }
        m_table->scrollTo(target, QAbstractItemView::PositionAtCenter);
    }

    m_updating = false;
    updateActionStates();
}
