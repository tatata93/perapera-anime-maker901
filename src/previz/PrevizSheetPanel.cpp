#include "PrevizSheetPanel.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

PrevizSheetPanel::PrevizSheetPanel(QWidget* parent) : QDockWidget(tr("プリビズシート"), parent) {
    setObjectName(QStringLiteral("PrevizSheetPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(container);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // キー有無の表示のみ、直接編集は不可
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table);

    setWidget(container);

    connect(m_table, &QTableWidget::cellClicked, this, &PrevizSheetPanel::onCellClicked);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &PrevizSheetPanel::onCellDoubleClicked);
}

void PrevizSheetPanel::onCellClicked(int row, int column) {
    if (m_updating) return;
    emit cellClicked(column, row);
}

void PrevizSheetPanel::onCellDoubleClicked(int row, int column) {
    if (m_updating) return;
    emit keyToggleRequested(column, row);
}

void PrevizSheetPanel::setSheet(const QStringList& columnNames, const QList<QList<bool>>& keyFlags, int frameCount,
                                 int currentFrame, int activeColumn) {
    m_updating = true;

    const int columnCount = columnNames.size();
    if (m_table->rowCount() != frameCount || m_table->columnCount() != columnCount) {
        m_table->clear();
        m_table->setRowCount(frameCount);
        m_table->setColumnCount(columnCount);
        for (int f = 0; f < frameCount; ++f) {
            for (int c = 0; c < columnCount; ++c) {
                m_table->setItem(f, c, new QTableWidgetItem());
            }
        }
    }

    m_table->setHorizontalHeaderLabels(columnNames);

    QStringList rowLabels;
    rowLabels.reserve(frameCount);
    for (int f = 0; f < frameCount; ++f) rowLabels << QString::number(f + 1);
    m_table->setVerticalHeaderLabels(rowLabels);

    for (int c = 0; c < columnCount; ++c) {
        const QList<bool>& column = c < keyFlags.size() ? keyFlags.at(c) : QList<bool>();
        for (int f = 0; f < frameCount; ++f) {
            QTableWidgetItem* item = m_table->item(f, c);
            const bool hasKey = f < column.size() && column.at(f);
            item->setText(hasKey ? QStringLiteral("●") : QString());  // ●
            item->setTextAlignment(Qt::AlignCenter);
        }
    }

    if (frameCount > 0 && columnCount > 0) {
        const int row = std::clamp(currentFrame, 0, frameCount - 1);
        const int col = std::clamp(activeColumn, 0, columnCount - 1);
        m_table->setCurrentCell(row, col, QItemSelectionModel::ClearAndSelect);
    }

    m_updating = false;
}
