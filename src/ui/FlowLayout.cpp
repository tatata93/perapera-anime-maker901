#include "FlowLayout.h"

#include <QSizePolicy>
#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    QLayoutItem* item = nullptr;
    while ((item = takeAt(0)) != nullptr) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) {
    m_items.append(item);
}

int FlowLayout::horizontalSpacing() const {
    return m_hSpace >= 0 ? m_hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const {
    return m_vSpace >= 0 ? m_vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return {};
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

int FlowLayout::count() const {
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const {
    return m_items.value(index);
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : m_items) {
        size = size.expandedTo(item->minimumSize());
    }
    QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QLayoutItem* FlowLayout::takeAt(int index) {
    if (index < 0 || index >= m_items.size()) return nullptr;
    return m_items.takeAt(index);
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const {
    const QMargins margins = contentsMargins();
    QRect effectiveRect = rect.adjusted(margins.left(), margins.top(), -margins.right(),
                                        -margins.bottom());
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    const int spaceX = horizontalSpacing();
    const int spaceY = verticalSpacing();

    for (QLayoutItem* item : m_items) {
        QWidget* widget = item->widget();
        const int styleSpaceX =
            spaceX >= 0 ? spaceX
                        : (widget ? widget->style()->layoutSpacing(QSizePolicy::PushButton,
                                                                    QSizePolicy::PushButton,
                                                                    Qt::Horizontal)
                                  : 0);
        const int styleSpaceY =
            spaceY >= 0 ? spaceY
                        : (widget ? widget->style()->layoutSpacing(QSizePolicy::PushButton,
                                                                    QSizePolicy::PushButton,
                                                                    Qt::Vertical)
                                  : 0);
        const QSize itemSize = item->sizeHint();
        const int nextX = x + itemSize.width() + styleSpaceX;

        if (x > effectiveRect.x() && nextX - styleSpaceX > effectiveRect.right() + 1) {
            x = effectiveRect.x();
            y += lineHeight + styleSpaceY;
            lineHeight = 0;
        }

        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), itemSize));

        x += itemSize.width() + styleSpaceX;
        lineHeight = qMax(lineHeight, itemSize.height());
    }

    return y + lineHeight - rect.y() + margins.bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric metric) const {
    QObject* parentObject = parent();
    if (!parentObject) return -1;
    if (parentObject->isWidgetType()) {
        auto* parentWidget = static_cast<QWidget*>(parentObject);
        return parentWidget->style()->pixelMetric(metric, nullptr, parentWidget);
    }
    return static_cast<QLayout*>(parentObject)->spacing();
}
