#include "ReferencePanel.h"

#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSize>
#include <QVBoxLayout>

namespace {
// 色指定リストのスウォッチアイコンの一辺サイズ
constexpr int kColorSpecSwatchSize = 16;
}  // namespace

ReferencePanel::ReferencePanel(QWidget* parent) : QDockWidget(tr("参照"), parent) {
    setObjectName(QStringLiteral("ReferencePanel"));  // レイアウト保存用の識別子

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    m_combo = new QComboBox(container);
    layout->addWidget(m_combo);

    m_imageLabel = new QLabel(container);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(80, 45);
    m_imageLabel->setStyleSheet(QStringLiteral("background-color: #808080; color: white;"));
    m_imageLabel->setText(tr("ボードなし"));
    layout->addWidget(m_imageLabel, 1);

    // 色指定(色指定書): 選択中ボードの色見本を一覧表示し、クリックでその色を拾えるようにする
    layout->addWidget(new QLabel(tr("色指定"), container));
    m_colorSpecList = new QListWidget(container);
    m_colorSpecList->setIconSize(QSize(kColorSpecSwatchSize, kColorSpecSwatchSize));
    m_colorSpecList->setMaximumHeight(120);
    layout->addWidget(m_colorSpecList);

    setWidget(container);

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating) return;
        emit boardSelected(index);
    });
    connect(m_colorSpecList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (m_updating || !item) return;
        emit colorPicked(item->data(Qt::UserRole).value<QColor>());
    });
}

void ReferencePanel::setBoards(const QStringList& names, int selectedIndex) {
    m_updating = true;
    m_combo->clear();
    m_combo->addItems(names);
    if (selectedIndex >= 0 && selectedIndex < m_combo->count()) {
        m_combo->setCurrentIndex(selectedIndex);
    }
    m_updating = false;
}

void ReferencePanel::setImage(const QImage& image) {
    if (image.isNull()) {
        m_composedImage = QImage();
        m_imageLabel->setPixmap(QPixmap());
        m_imageLabel->setText(tr("ボードなし"));
        return;
    }

    // 白背景合成(ボードの透明部分を白紙として見せる。サムネイル合成と同じ考え方)
    QImage composed(image.size(), QImage::Format_RGB32);
    composed.fill(Qt::white);
    QPainter painter(&composed);
    painter.drawImage(0, 0, image);
    painter.end();

    m_imageLabel->setText(QString());
    m_composedImage = composed;
    applyScaledPixmap();
}

void ReferencePanel::setColorSpecs(const QList<QPair<QString, QColor>>& specs) {
    m_updating = true;
    m_colorSpecList->clear();
    for (const auto& [name, color] : specs) {
        QPixmap pixmap(kColorSpecSwatchSize, kColorSpecSwatchSize);
        pixmap.fill(color);
        auto* item = new QListWidgetItem(QIcon(pixmap), name);
        item->setData(Qt::UserRole, color);
        m_colorSpecList->addItem(item);
    }
    m_updating = false;
}

void ReferencePanel::resizeEvent(QResizeEvent* event) {
    QDockWidget::resizeEvent(event);
    applyScaledPixmap();
}

void ReferencePanel::applyScaledPixmap() {
    if (m_composedImage.isNull()) return;
    m_imageLabel->setPixmap(QPixmap::fromImage(
        m_composedImage.scaled(m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}
