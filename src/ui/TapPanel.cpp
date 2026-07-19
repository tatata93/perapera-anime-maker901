#include "TapPanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DockScrollArea.h"

TapPanel::TapPanel(QWidget* parent) : QDockWidget(tr("タップ"), parent) {
    setObjectName(QStringLiteral("TapPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(container);
    layout->addWidget(m_list);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("現在コマにキー"), container);
    auto* removeButton = new QPushButton(tr("キー削除"), container);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    layout->addLayout(buttonLayout);

    perapera::ui::setScrollableDockWidget(this, container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0) return;
        QListWidgetItem* item = m_list->item(row);
        if (item) emit keySelected(item->data(Qt::UserRole).toInt());
    });
    connect(addButton, &QPushButton::clicked, this, &TapPanel::addKeyRequested);
    connect(removeButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = m_list->currentItem();
        if (item) emit removeKeyRequested(item->data(Qt::UserRole).toInt());
    });
}

void TapPanel::setKeys(const QList<std::tuple<int, float, float>>& keys, int currentFrame) {
    m_updating = true;

    m_list->clear();
    int selectRow = -1;
    for (int i = 0; i < keys.size(); ++i) {
        const int frame = std::get<0>(keys.at(i));
        const float x = std::get<1>(keys.at(i));
        const float y = std::get<2>(keys.at(i));

        // 表示はコマを1始まりにする(タイムシート/フレームラベルと同じ表記に合わせる)
        auto* item = new QListWidgetItem(
            tr("コマ %1: (%2, %3)").arg(frame + 1).arg(static_cast<int>(x)).arg(static_cast<int>(y)));
        item->setData(Qt::UserRole, frame);
        m_list->addItem(item);
        if (frame == currentFrame) selectRow = i;
    }
    if (selectRow >= 0) m_list->setCurrentRow(selectRow);

    m_updating = false;
}
