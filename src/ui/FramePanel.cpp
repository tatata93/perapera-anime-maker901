#include "FramePanel.h"

#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

FramePanel::FramePanel(QWidget* parent) : QDockWidget(tr("動画"), parent) {
    setObjectName(QStringLiteral("FramePanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_sortCombo = new QComboBox(container);
    m_sortCombo->addItem(tr("番号順"));
    m_sortCombo->addItem(tr("再生順"));
    layout->addWidget(m_sortCombo);

    m_list = new QListWidget(container);
    layout->addWidget(m_list);

    m_addButton = new QPushButton(tr("動画追加"), container);
    layout->addWidget(m_addButton);

    m_deleteButton = new QPushButton(tr("動画削除"), container);
    layout->addWidget(m_deleteButton);

    setWidget(container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0 || row >= m_displayOrder.size()) return;
        emit frameSelected(m_displayOrder.at(row));
    });
    connect(m_addButton, &QPushButton::clicked, this, &FramePanel::addRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_displayOrder.size()) return;  // 未選択なら発火しない
        emit deleteRequested(m_displayOrder.at(row));
    });
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_updating) emit sortModeChanged();
    });
}

int FramePanel::sortMode() const {
    return m_sortCombo->currentIndex();
}

void FramePanel::setDrawings(const QList<int>& displayOrder, int currentDrawing) {
    m_updating = true;
    m_displayOrder = displayOrder;

    const int count = displayOrder.size();
    if (m_list->count() != count) {
        m_list->clear();
        for (int row = 0; row < count; ++row) {
            m_list->addItem(QString());
        }
    }
    for (int row = 0; row < count; ++row) {
        m_list->item(row)->setText(tr("動画 %1").arg(displayOrder.at(row) + 1));
    }

    m_list->setCurrentRow(displayOrder.indexOf(currentDrawing));  // 見つからなければ-1(選択なし)

    m_updating = false;
}
