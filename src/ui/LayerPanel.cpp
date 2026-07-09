#include "LayerPanel.h"

#include <QAction>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(tr("レイヤー"), parent) {
    setObjectName(QStringLiteral("LayerPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(container);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("追加"), container);
    auto* removeButton = new QPushButton(tr("削除"), container);
    auto* upButton = new QPushButton(tr("上へ"), container);
    auto* downButton = new QPushButton(tr("下へ"), container);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addWidget(upButton);
    buttonLayout->addWidget(downButton);
    layout->addLayout(buttonLayout);

    setWidget(container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) emit layerSelected(rowToLayerIndex(row));
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating) return;
        const int row = m_list->row(item);
        emit visibilityChanged(rowToLayerIndex(row), item->checkState() == Qt::Checked);
    });
    connect(addButton, &QPushButton::clicked, this, &LayerPanel::addRequested);
    connect(removeButton, &QPushButton::clicked, this, &LayerPanel::removeRequested);
    connect(upButton, &QPushButton::clicked, this, &LayerPanel::moveUpRequested);
    connect(downButton, &QPushButton::clicked, this, &LayerPanel::moveDownRequested);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &LayerPanel::showContextMenu);
}

void LayerPanel::showContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item) return;
    const int layerIndex = rowToLayerIndex(m_list->row(item));

    QMenu menu(this);
    QAction* normalAction = menu.addAction(tr("通常"));
    QAction* colorTraceAction = menu.addAction(tr("色トレス線"));
    QAction* correctionAction = menu.addAction(tr("作監修正"));

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == normalAction) {
        emit roleChangeRequested(layerIndex, 0);
    } else if (chosen == colorTraceAction) {
        emit roleChangeRequested(layerIndex, 1);
    } else if (chosen == correctionAction) {
        emit roleChangeRequested(layerIndex, 2);
    }
}

int LayerPanel::rowToLayerIndex(int row) const {
    return m_list->count() - 1 - row;
}

int LayerPanel::layerIndexToRow(int layerIndex) const {
    return m_list->count() - 1 - layerIndex;
}

void LayerPanel::setLayers(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop, int activeIndex) {
    m_updating = true;

    const int count = namesBottomToTop.size();
    if (m_list->count() != count) {
        m_list->clear();
        for (int row = 0; row < count; ++row) {
            auto* item = new QListWidgetItem();
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            m_list->addItem(item);
        }
    }

    // リスト行0=最上位レイヤーとなるよう、コア側(下から上)の配列を反転して割り当てる
    for (int row = 0; row < count; ++row) {
        const int layerIndex = rowToLayerIndex(row);
        QListWidgetItem* item = m_list->item(row);
        item->setText(namesBottomToTop.at(layerIndex));
        item->setCheckState(visibleBottomToTop.at(layerIndex) ? Qt::Checked : Qt::Unchecked);
    }

    m_list->setCurrentRow(layerIndexToRow(activeIndex));

    m_updating = false;
}
