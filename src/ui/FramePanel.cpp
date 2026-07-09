#include "FramePanel.h"

#include <QListWidget>

FramePanel::FramePanel(QWidget* parent) : QDockWidget(tr("動画"), parent) {
    setObjectName(QStringLiteral("FramePanel"));  // レイアウト保存用の識別子

    m_list = new QListWidget(this);
    setWidget(m_list);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) emit frameSelected(row);
    });
}

void FramePanel::setFrames(int count, int currentIndex) {
    m_updating = true;
    if (m_list->count() != count) {
        m_list->clear();
        for (int i = 0; i < count; ++i) {
            m_list->addItem(tr("動画 %1").arg(i + 1));
        }
    }
    m_list->setCurrentRow(currentIndex);
    m_updating = false;
}
