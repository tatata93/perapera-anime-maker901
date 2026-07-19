#include "CameraPanel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/DockScrollArea.h"

CameraPanel::CameraPanel(QWidget* parent) : QDockWidget(tr("カメラフレーム"), parent) {
    setObjectName(QStringLiteral("CameraPanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    const auto addRow = [container, layout](const QString& label, QWidget* w) {
        auto* row = new QWidget(container);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(label, row));
        h->addWidget(w, 1);
        layout->addWidget(row);
    };

    m_centerXSpin = new QDoubleSpinBox(container);
    m_centerXSpin->setRange(-100000.0, 100000.0);
    m_centerXSpin->setDecimals(1);
    m_centerXSpin->setFocusPolicy(Qt::ClickFocus);
    addRow(tr("中心X"), m_centerXSpin);

    m_centerYSpin = new QDoubleSpinBox(container);
    m_centerYSpin->setRange(-100000.0, 100000.0);
    m_centerYSpin->setDecimals(1);
    m_centerYSpin->setFocusPolicy(Qt::ClickFocus);
    addRow(tr("中心Y"), m_centerYSpin);

    m_scaleSpin = new QDoubleSpinBox(container);
    m_scaleSpin->setRange(5.0, 400.0);
    m_scaleSpin->setDecimals(0);
    m_scaleSpin->setSuffix(tr(" %"));
    m_scaleSpin->setFocusPolicy(Qt::ClickFocus);
    addRow(tr("スケール"), m_scaleSpin);

    m_keyStateLabel = new QLabel(tr("キー: なし"), container);
    layout->addWidget(m_keyStateLabel);

    auto* buttonLayout = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("キー追加"), container);
    auto* removeButton = new QPushButton(tr("キー削除"), container);
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    layout->addLayout(buttonLayout);

    auto* clearButton = new QPushButton(tr("全キー削除"), container);
    layout->addWidget(clearButton);

    m_showFrameCheck = new QCheckBox(tr("枠を表示"), container);
    m_showFrameCheck->setChecked(true);  // 既定ON
    layout->addWidget(m_showFrameCheck);

    layout->addStretch(1);
    perapera::ui::setScrollableDockWidget(this, container);

    const auto emitValuesChanged = [this] {
        if (m_updating) return;
        emit valuesChanged(m_centerXSpin->value(), m_centerYSpin->value(), m_scaleSpin->value());
    };
    connect(m_centerXSpin, &QDoubleSpinBox::valueChanged, this, [emitValuesChanged](double) { emitValuesChanged(); });
    connect(m_centerYSpin, &QDoubleSpinBox::valueChanged, this, [emitValuesChanged](double) { emitValuesChanged(); });
    connect(m_scaleSpin, &QDoubleSpinBox::valueChanged, this, [emitValuesChanged](double) { emitValuesChanged(); });

    connect(addButton, &QPushButton::clicked, this, &CameraPanel::addKeyRequested);
    connect(removeButton, &QPushButton::clicked, this, &CameraPanel::removeKeyRequested);
    connect(clearButton, &QPushButton::clicked, this, &CameraPanel::clearAllKeysRequested);
    connect(m_showFrameCheck, &QCheckBox::toggled, this, &CameraPanel::showFrameToggled);
}

void CameraPanel::setValues(double centerX, double centerY, double scalePercent) {
    m_updating = true;
    m_centerXSpin->setValue(centerX);
    m_centerYSpin->setValue(centerY);
    m_scaleSpin->setValue(scalePercent);
    m_updating = false;
}

void CameraPanel::setKeyState(bool hasKeyOnFrame, bool hasAnyKeys) {
    if (hasKeyOnFrame) {
        m_keyStateLabel->setText(tr("キー: あり"));
    } else if (hasAnyKeys) {
        m_keyStateLabel->setText(tr("キー: なし(補間)"));
    } else {
        m_keyStateLabel->setText(tr("キー: なし"));
    }
}

double CameraPanel::centerX() const { return m_centerXSpin->value(); }
double CameraPanel::centerY() const { return m_centerYSpin->value(); }
double CameraPanel::scalePercent() const { return m_scaleSpin->value(); }
bool CameraPanel::showFrameEnabled() const { return m_showFrameCheck->isChecked(); }
