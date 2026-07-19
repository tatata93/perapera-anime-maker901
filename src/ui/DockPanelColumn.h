#pragma once

#include <QAbstractScrollArea>
#include <QDockWidget>
#include <QFrame>
#include <QLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace perapera::ui {

inline constexpr const char* kSkipRetroDockTitleProperty = "peraperaSkipRetroDockTitle";

class DockPanelColumn : public QDockWidget {
public:
    explicit DockPanelColumn(const QString& objectName, QWidget* parent = nullptr) : QDockWidget(parent) {
        setObjectName(objectName);
        setFeatures(QDockWidget::NoDockWidgetFeatures);
        setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        setProperty(kSkipRetroDockTitleProperty, true);

        auto* hiddenTitle = new QWidget(this);
        hiddenTitle->setFixedHeight(0);
        setTitleBarWidget(hiddenTitle);

        auto* scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        m_container = new QWidget(scroll);
        m_layout = new QVBoxLayout(m_container);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(4);
        m_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        m_layout->addStretch(1);

        scroll->setWidget(m_container);
        setWidget(scroll);
        setMinimumWidth(150);
    }

    void addPanel(QDockWidget* panel) {
        if (!panel || !m_layout) return;
        panel->setParent(m_container);
        panel->setFloating(false);
        panel->setFeatures(QDockWidget::DockWidgetClosable);
        panel->setMinimumSize(0, 0);
        panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_layout->insertWidget(m_layout->count() - 1, panel);
    }

private:
    QWidget* m_container = nullptr;
    QVBoxLayout* m_layout = nullptr;
};

}  // namespace perapera::ui
