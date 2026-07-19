#pragma once

#include <QAbstractScrollArea>
#include <QDockWidget>
#include <QFrame>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QWidget>

namespace perapera::ui {

inline QScrollArea* setScrollableDockWidget(QDockWidget* dock, QWidget* content) {
    auto* scroll = new QScrollArea(dock);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMinimumSize(QSize(0, 0));
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    if (content) {
        content->setMinimumSize(QSize(0, 0));
        content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        scroll->setWidget(content);
    }

    if (dock) {
        dock->setMinimumSize(QSize(120, 54));
        dock->setWidget(scroll);
    }
    return scroll;
}

}  // namespace perapera::ui
