#include "PalettePanel.h"

#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DockScrollArea.h"

namespace {
constexpr int kSwatchSize = 24;
}  // namespace

PalettePanel::PalettePanel(QWidget* parent) : QDockWidget(tr("カラーパレット"), parent) {
    setObjectName(QStringLiteral("PalettePanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(container);
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(kSwatchSize, kSwatchSize));
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setMovement(QListWidget::Static);
    layout->addWidget(m_list);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("現在色を追加"), container);
    auto* removeButton = new QPushButton(tr("削除"), container);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    layout->addLayout(buttonLayout);

    perapera::ui::setScrollableDockWidget(this, container);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0) return;
        const QColor color = m_list->item(row)->data(Qt::UserRole).value<QColor>();
        emit colorSelected(color);
    });
    connect(addButton, &QPushButton::clicked, this, &PalettePanel::addCurrentColorRequested);
    connect(removeButton, &QPushButton::clicked, this, &PalettePanel::removeSelectedRequested);
}

void PalettePanel::setPalette(const QList<QColor>& colors) {
    m_updating = true;

    m_list->clear();
    for (const QColor& color : colors) {
        QPixmap pixmap(kSwatchSize, kSwatchSize);
        pixmap.fill(color);
        auto* item = new QListWidgetItem(QIcon(pixmap), QString());
        item->setData(Qt::UserRole, color);
        m_list->addItem(item);
    }

    m_updating = false;
}

int PalettePanel::selectedIndex() const {
    return m_list->currentRow();
}
