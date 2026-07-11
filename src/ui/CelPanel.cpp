#include "CelPanel.h"

#include <QFont>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

CelPanel::CelPanel(QWidget* parent) : QDockWidget(tr("セル"), parent) {
    setObjectName(QStringLiteral("CelPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(container);
    layout->addWidget(m_list);

    // 引きセル: アクティブセルの用紙サイズ(キャンバスより大きい紙にしてパンを再現)を変更する
    auto* sizeButton = new QPushButton(tr("セルサイズ..."), container);
    connect(sizeButton, &QPushButton::clicked, this, &CelPanel::celSizeRequested);
    layout->addWidget(sizeButton);

    setWidget(container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) emit celSelected(rowToCelIndex(row));
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating) return;
        const int row = m_list->row(item);
        emit visibilityChanged(rowToCelIndex(row), item->checkState() == Qt::Checked);
    });
}

int CelPanel::rowToCelIndex(int row) const {
    return m_list->count() - 1 - row;
}

int CelPanel::celIndexToRow(int celIndex) const {
    return m_list->count() - 1 - celIndex;
}

void CelPanel::setCels(const QStringList& namesBottomToTop, const QList<bool>& visibleBottomToTop, int activeIndex) {
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

    // リスト行0=最前面セルとなるよう、コア側(最奥から最前面)の配列を反転して割り当てる
    for (int row = 0; row < count; ++row) {
        const int celIndex = rowToCelIndex(row);
        QListWidgetItem* item = m_list->item(row);
        item->setText(namesBottomToTop.at(celIndex));
        item->setCheckState(visibleBottomToTop.at(celIndex) ? Qt::Checked : Qt::Unchecked);

        QFont font = item->font();
        font.setBold(celIndex == activeIndex);
        item->setFont(font);
    }

    m_list->setCurrentRow(celIndexToRow(activeIndex));

    m_updating = false;
}
