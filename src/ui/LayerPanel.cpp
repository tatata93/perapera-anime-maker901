#include "LayerPanel.h"

#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DockScrollArea.h"

LayerPanel::LayerPanel(QWidget* parent) : QDockWidget(tr("レイヤー"), parent) {
    setObjectName(QStringLiteral("LayerPanel"));
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_list = new QListWidget(container);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list);

    auto* opacityRow = new QHBoxLayout();
    m_opacityLabel = new QLabel(container);
    m_opacitySlider = new QSlider(Qt::Horizontal, container);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setSingleStep(5);
    m_opacitySlider->setPageStep(10);
    opacityRow->addWidget(m_opacityLabel);
    opacityRow->addWidget(m_opacitySlider, 1);
    layout->addLayout(opacityRow);
    updateOpacityLabel(100);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("追加"), container);
    auto* duplicateButton = new QPushButton(tr("複製"), container);
    auto* removeButton = new QPushButton(tr("削除"), container);
    auto* upButton = new QPushButton(tr("上へ"), container);
    auto* downButton = new QPushButton(tr("下へ"), container);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(duplicateButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addWidget(upButton);
    buttonLayout->addWidget(downButton);
    layout->addLayout(buttonLayout);

    perapera::ui::setScrollableDockWidget(this, container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) emit layerSelected(rowToLayerIndex(row));
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating) return;
        const int row = m_list->row(item);
        emit visibilityChanged(rowToLayerIndex(row), item->checkState() == Qt::Checked);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit renameRequested(rowToLayerIndex(m_list->row(item)));
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &LayerPanel::showContextMenu);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        updateOpacityLabel(value);
        if (!m_updating && m_activeLayerIndex >= 0) emit opacityChanged(m_activeLayerIndex, value);
    });
    connect(addButton, &QPushButton::clicked, this, &LayerPanel::addRequested);
    connect(duplicateButton, &QPushButton::clicked, this, [this] {
        if (m_activeLayerIndex >= 0) emit duplicateRequested(m_activeLayerIndex);
    });
    connect(removeButton, &QPushButton::clicked, this, &LayerPanel::removeRequested);
    connect(upButton, &QPushButton::clicked, this, &LayerPanel::moveUpRequested);
    connect(downButton, &QPushButton::clicked, this, &LayerPanel::moveDownRequested);
}

void LayerPanel::showContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item) return;
    const int layerIndex = rowToLayerIndex(m_list->row(item));

    QMenu menu(this);
    QAction* duplicateAction = menu.addAction(tr("レイヤーを複製"));
    QAction* renameAction = menu.addAction(tr("名前を変更"));
    menu.addSeparator();
    QAction* normalAction = menu.addAction(tr("通常"));
    QAction* colorTraceAction = menu.addAction(tr("色トレス線"));
    QAction* correctionAction = menu.addAction(tr("作画修正"));

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == duplicateAction) {
        emit duplicateRequested(layerIndex);
    } else if (chosen == renameAction) {
        emit renameRequested(layerIndex);
    } else if (chosen == normalAction) {
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

void LayerPanel::updateOpacityLabel(int percent) {
    if (m_opacityLabel) m_opacityLabel->setText(tr("不透明度 %1%").arg(percent));
}

void LayerPanel::setLayers(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop,
                           const QList<int>& opacityPercentsBottomToTop, int activeIndex) {
    m_updating = true;
    m_activeLayerIndex = activeIndex;

    const int count = namesBottomToTop.size();
    if (m_list->count() != count) {
        m_list->clear();
        for (int row = 0; row < count; ++row) {
            auto* item = new QListWidgetItem();
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            m_list->addItem(item);
        }
    }

    for (int row = 0; row < count; ++row) {
        const int layerIndex = rowToLayerIndex(row);
        const int opacity = opacityPercentsBottomToTop.value(layerIndex, 100);
        QListWidgetItem* item = m_list->item(row);
        item->setText(tr("%1  %2%").arg(namesBottomToTop.at(layerIndex)).arg(opacity));
        item->setCheckState(visibleBottomToTop.at(layerIndex) ? Qt::Checked : Qt::Unchecked);

        QFont font = item->font();
        font.setBold(layerIndex == activeIndex);
        item->setFont(font);
    }

    m_list->setCurrentRow(layerIndexToRow(activeIndex));
    const int activeOpacity = opacityPercentsBottomToTop.value(activeIndex, 100);
    m_opacitySlider->setValue(activeOpacity);
    updateOpacityLabel(activeOpacity);
    m_opacitySlider->setEnabled(activeIndex >= 0 && count > 0);

    m_updating = false;
}
