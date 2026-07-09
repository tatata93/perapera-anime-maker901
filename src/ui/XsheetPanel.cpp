#include "XsheetPanel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

XsheetPanel::XsheetPanel(QWidget* parent) : QDockWidget(tr("タイムシート"), parent) {
    setObjectName(QStringLiteral("XsheetPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    // 上段: 尺スピンボックス + コマ打ちボタン
    auto* toolLayout = new QHBoxLayout();
    toolLayout->addWidget(new QLabel(tr("尺:"), container));
    m_frameCountSpin = new QSpinBox(container);
    m_frameCountSpin->setRange(1, 9999);
    toolLayout->addWidget(m_frameCountSpin);

    auto* step1Button = new QPushButton(tr("1コマ"), container);
    auto* step2Button = new QPushButton(tr("2コマ"), container);
    auto* step3Button = new QPushButton(tr("3コマ"), container);
    toolLayout->addWidget(step1Button);
    toolLayout->addWidget(step2Button);
    toolLayout->addWidget(step3Button);

    toolLayout->addSpacing(16);  // コマ打ちボタンとセル操作ボタンの間の区切り余白

    auto* celAddButton = new QPushButton(tr("セル追加"), container);
    auto* celRemoveButton = new QPushButton(tr("セル削除"), container);
    auto* celRenameButton = new QPushButton(tr("名前変更"), container);
    auto* celMovePrevButton = new QPushButton(tr("←前へ"), container);
    auto* celMoveNextButton = new QPushButton(tr("後ろへ→"), container);
    toolLayout->addWidget(celAddButton);
    toolLayout->addWidget(celRemoveButton);
    toolLayout->addWidget(celRenameButton);
    toolLayout->addWidget(celMovePrevButton);
    toolLayout->addWidget(celMoveNextButton);
    toolLayout->addStretch();
    layout->addLayout(toolLayout);

    m_table = new QTableWidget(container);
    // 行(コマ)単位で選択表示する。セル欄の編集自体は選択挙動と独立して行える
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);  // 列ヘッダ右クリックで表示/非表示切替
    layout->addWidget(m_table);

    setWidget(container);

    connect(m_frameCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updating) emit frameCountChanged(value);
    });
    connect(step1Button, &QPushButton::clicked, this, [this] { emit stepPatternRequested(1); });
    connect(step2Button, &QPushButton::clicked, this, [this] { emit stepPatternRequested(2); });
    connect(step3Button, &QPushButton::clicked, this, [this] { emit stepPatternRequested(3); });
    connect(celAddButton, &QPushButton::clicked, this, &XsheetPanel::celAddRequested);
    connect(celRemoveButton, &QPushButton::clicked, this, &XsheetPanel::celRemoveRequested);
    connect(celRenameButton, &QPushButton::clicked, this, &XsheetPanel::celRenameRequested);
    connect(celMovePrevButton, &QPushButton::clicked, this, [this] { emit celMoveRequested(-1); });
    connect(celMoveNextButton, &QPushButton::clicked, this, [this] { emit celMoveRequested(+1); });

    connect(m_table, &QTableWidget::itemChanged, this, &XsheetPanel::onItemChanged);
    connect(m_table, &QTableWidget::cellClicked, this, &XsheetPanel::onCellClicked);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            &XsheetPanel::showHeaderContextMenu);
}

void XsheetPanel::onItemChanged(QTableWidgetItem* item) {
    if (m_updating) return;
    const int frame = item->row();
    const int celIndex = item->column();

    const QString text = item->text().trimmed();
    int drawing = -1;  // 空欄扱い
    if (!text.isEmpty()) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (ok && value > 0) drawing = value - 1;  // 1始まり表示 → 0始まりの動画番号
    }
    emit exposureEdited(celIndex, frame, drawing);
}

void XsheetPanel::onCellClicked(int row, int column) {
    emit cellClicked(column, row);
}

void XsheetPanel::showHeaderContextMenu(const QPoint& pos) {
    QHeaderView* header = m_table->horizontalHeader();
    const int celIndex = header->logicalIndexAt(pos);
    if (celIndex < 0) return;

    QMenu menu(this);
    QAction* toggleAction = menu.addAction(tr("表示/非表示"));
    QAction* chosen = menu.exec(header->viewport()->mapToGlobal(pos));
    if (chosen == toggleAction) {
        emit celVisibilityToggleRequested(celIndex);
    }
}

void XsheetPanel::setSheet(const QStringList& celNames, const QList<bool>& celVisible,
                            const QList<QList<int>>& exposures, int frameCount, int currentFrame, int activeCel) {
    m_updating = true;

    m_frameCountSpin->setValue(frameCount);

    const int celCount = celNames.size();
    if (m_table->rowCount() != frameCount || m_table->columnCount() != celCount) {
        m_table->clear();
        m_table->setRowCount(frameCount);
        m_table->setColumnCount(celCount);
        for (int f = 0; f < frameCount; ++f) {
            for (int c = 0; c < celCount; ++c) {
                m_table->setItem(f, c, new QTableWidgetItem());
            }
        }
    }

    // 列ヘッダ: 非表示セルには「 (非表示)」を付け、アクティブセルは太字にする
    for (int c = 0; c < celCount; ++c) {
        QString label = celNames.at(c);
        const bool visible = c < celVisible.size() ? celVisible.at(c) : true;
        if (!visible) label += tr(" (非表示)");
        auto* headerItem = new QTableWidgetItem(label);
        QFont font = headerItem->font();
        font.setBold(c == activeCel);
        headerItem->setFont(font);
        m_table->setHorizontalHeaderItem(c, headerItem);
    }

    QStringList rowLabels;
    rowLabels.reserve(frameCount);
    for (int f = 0; f < frameCount; ++f) rowLabels << QString::number(f + 1);
    m_table->setVerticalHeaderLabels(rowLabels);

    for (int c = 0; c < celCount; ++c) {
        const QList<int>& column = exposures.at(c);
        for (int f = 0; f < frameCount; ++f) {
            QTableWidgetItem* item = m_table->item(f, c);
            const int drawing = f < column.size() ? column.at(f) : -1;
            item->setText(drawing >= 0 ? QString::number(drawing + 1) : QString());
        }
    }

    if (frameCount > 0) {
        const int row = std::clamp(currentFrame, 0, frameCount - 1);
        m_table->selectRow(row);  // 現在コマの行を選択表示する
        if (celCount > 0) {
            const int col = std::clamp(activeCel, 0, celCount - 1);
            // selectRowによる行選択を崩さず、キーボード操作用のカレントセルだけ設定する
            m_table->setCurrentCell(row, col, QItemSelectionModel::NoUpdate);
        }
    }

    m_updating = false;
}
