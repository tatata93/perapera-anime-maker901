#include "FilmCurveWidget.h"

#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kPointCount = 5;
constexpr double kIdentityPts[kPointCount] = {0.0, 0.25, 0.5, 0.75, 1.0};
constexpr double kHitRadiusPx = 9.0;  // 制御点のクリック判定半径(px)

// グラフ枠の余白(左=縦軸ラベル用、下=横軸ラベル用)
constexpr int kMarginLeft = 8;
constexpr int kMarginRight = 8;
constexpr int kMarginTop = 8;
constexpr int kMarginBottom = 20;

QColor layerColor(int layer) {
    switch (layer) {
        case 0:
            return QColor(230, 80, 80);   // R
        case 1:
            return QColor(70, 190, 100);  // G
        default:
            return QColor(90, 140, 235);  // B
    }
}

}  // namespace

// グラフ本体: 3層(R/G/B)の5点カーブを描画し、選択中の層(または「全て」時は各点の最近傍層)の
// 制御点を縦方向にドラッグできる。制御点の横位置は常に固定(0,0.25,0.5,0.75,1.0)
class FilmCurveWidget::GraphArea : public QWidget {
public:
    explicit GraphArea(FilmCurveWidget* owner) : QWidget(owner), m_owner(owner) {
        setMinimumSize(240, 180);
        for (auto& layerPts : points) {
            for (int i = 0; i < kPointCount; ++i) layerPts[i] = kIdentityPts[i];
        }
    }

    void setLayerPoints(int layer, const double pts[kPointCount]) {
        if (layer < 0 || layer > 2) return;
        for (int i = 0; i < kPointCount; ++i) points[layer][i] = std::clamp(pts[i], 0.0, 1.0);
        update();
    }

    void setSelectedLayer(int layer) {
        selectedLayer = layer;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF area = graphRect();

        // 枠+グリッド(4分割)
        painter.setPen(QPen(QColor(140, 140, 140), 1));
        painter.drawRect(area);
        painter.setPen(QPen(QColor(90, 90, 90), 1, Qt::DotLine));
        for (int i = 1; i < 4; ++i) {
            const double gx = area.left() + area.width() * i / 4.0;
            const double gy = area.top() + area.height() * i / 4.0;
            painter.drawLine(QPointF(gx, area.top()), QPointF(gx, area.bottom()));
            painter.drawLine(QPointF(area.left(), gy), QPointF(area.right(), gy));
        }

        // 対角線(恒等カーブの目安)を薄く
        painter.setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
        painter.drawLine(area.bottomLeft(), area.topRight());

        // 選択中の層(または「全て」時は全層)を前面に描く: 非選択層は選択層より先に薄く描く
        for (int layer = 0; layer < 3; ++layer) {
            if (selectedLayer >= 0 && layer != selectedLayer) drawLayerCurve(painter, area, layer, false);
        }
        if (selectedLayer >= 0) {
            drawLayerCurve(painter, area, selectedLayer, true);
        } else {
            for (int layer = 0; layer < 3; ++layer) drawLayerCurve(painter, area, layer, true);
        }

        // 軸ラベル
        painter.setPen(QColor(200, 200, 200));
        QFont axisFont = painter.font();
        axisFont.setPointSizeF(std::max(7.0, axisFont.pointSizeF() - 2.0));
        painter.setFont(axisFont);
        painter.drawText(QRectF(area.left(), area.bottom() + 2, area.width(), kMarginBottom - 2),
                          Qt::AlignHCenter | Qt::AlignTop, tr("光の強さ →"));
        painter.save();
        painter.translate(10, area.bottom());
        painter.rotate(-90);
        painter.drawText(QRectF(0, -12, area.height(), 12), Qt::AlignLeft | Qt::AlignVCenter, tr("記録量 →"));
        painter.restore();
    }

    void mousePressEvent(QMouseEvent* event) override {
        int hitLayer = -1, hitIndex = -1;
        findNearestPoint(event->pos(), hitLayer, hitIndex);
        if (hitLayer < 0) return;
        m_dragLayer = hitLayer;
        m_dragIndex = hitIndex;
        applyDrag(event->pos());
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragLayer < 0) return;
        applyDrag(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_dragLayer < 0) return;
        applyDrag(event->pos());
        m_dragLayer = -1;
        m_dragIndex = -1;
    }

private:
    QRectF graphRect() const {
        return QRectF(kMarginLeft, kMarginTop, std::max(1, width() - kMarginLeft - kMarginRight),
                       std::max(1, height() - kMarginTop - kMarginBottom));
    }

    // カーブ座標(x,y ともに0〜1)→ウィジェット座標(yは上下反転: 記録量1.0が上)
    QPointF toScreen(const QRectF& area, double x, double y) const {
        return QPointF(area.left() + x * area.width(), area.bottom() - y * area.height());
    }

    void drawLayerCurve(QPainter& painter, const QRectF& area, int layer, bool prominent) {
        QColor color = layerColor(layer);
        if (!prominent) color.setAlpha(90);

        painter.setPen(QPen(color, prominent ? 2.5 : 1.5));
        QPointF prev = toScreen(area, 0.0, points[layer][0]);
        for (int i = 1; i < kPointCount; ++i) {
            const QPointF cur = toScreen(area, i / static_cast<double>(kPointCount - 1), points[layer][i]);
            painter.drawLine(prev, cur);
            prev = cur;
        }

        painter.setBrush(color);
        painter.setPen(QPen(color.darker(130), 1));
        const double r = prominent ? 5.0 : 3.0;
        for (int i = 0; i < kPointCount; ++i) {
            const QPointF c = toScreen(area, i / static_cast<double>(kPointCount - 1), points[layer][i]);
            painter.drawEllipse(c, r, r);
        }
    }

    // クリック/ドラッグ開始位置に最も近い制御点を探す。「全て」選択時は3層すべてから、
    // 単一層選択時はその層の点だけを対象にする(spec: ドラッグは各点の最近傍層)
    void findNearestPoint(const QPoint& pos, int& outLayer, int& outIndex) const {
        outLayer = -1;
        outIndex = -1;
        const QRectF area = graphRect();
        double best = kHitRadiusPx * kHitRadiusPx;
        const int layerFrom = selectedLayer >= 0 ? selectedLayer : 0;
        const int layerTo = selectedLayer >= 0 ? selectedLayer : 2;
        for (int layer = layerFrom; layer <= layerTo; ++layer) {
            for (int i = 0; i < kPointCount; ++i) {
                const QPointF c = toScreen(area, i / static_cast<double>(kPointCount - 1), points[layer][i]);
                const double dx = c.x() - pos.x();
                const double dy = c.y() - pos.y();
                const double d2 = dx * dx + dy * dy;
                if (d2 < best) {
                    best = d2;
                    outLayer = layer;
                    outIndex = i;
                }
            }
        }
    }

    void applyDrag(const QPoint& pos) {
        const QRectF area = graphRect();
        double t = area.height() > 0 ? (area.bottom() - pos.y()) / area.height() : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        points[m_dragLayer][m_dragIndex] = t;
        update();
        m_owner->reportPointDrag(m_dragLayer, m_dragIndex, t);
    }

    FilmCurveWidget* m_owner = nullptr;
    int m_dragLayer = -1;
    int m_dragIndex = -1;

public:
    std::array<std::array<double, kPointCount>, 3> points{};
    int selectedLayer = 0;  // -1=全て、0=R、1=G、2=B
};

FilmCurveWidget::FilmCurveWidget(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* controlRow = new QHBoxLayout();
    controlRow->addWidget(new QLabel(tr("編集する層:"), this));

    m_layerCombo = new QComboBox(this);
    m_layerCombo->addItem(tr("R"), 0);
    m_layerCombo->addItem(tr("G"), 1);
    m_layerCombo->addItem(tr("B"), 2);
    m_layerCombo->addItem(tr("全て"), -1);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &FilmCurveWidget::onLayerComboChanged);
    controlRow->addWidget(m_layerCombo);

    auto* resetButton = new QPushButton(tr("リセット"), this);
    resetButton->setToolTip(tr("選択中の層(「全て」の場合は全層)のカーブを恒等へ戻す"));
    connect(resetButton, &QPushButton::clicked, this, &FilmCurveWidget::onResetClicked);
    controlRow->addWidget(resetButton);
    controlRow->addStretch(1);

    mainLayout->addLayout(controlRow);

    m_graph = new GraphArea(this);
    mainLayout->addWidget(m_graph, 1);
}

void FilmCurveWidget::setPoints(int layer, const double pts[5]) {
    if (!m_graph) return;
    m_updating = true;
    m_graph->setLayerPoints(layer, pts);
    m_updating = false;
}

void FilmCurveWidget::reportPointDrag(int layer, int pointIndex, double value) {
    if (m_updating) return;
    emit curveChanged(layer, pointIndex, value);
}

void FilmCurveWidget::onLayerComboChanged(int index) {
    if (!m_layerCombo || !m_graph) return;
    const int layer = m_layerCombo->itemData(index).toInt();
    m_graph->setSelectedLayer(layer);
}

void FilmCurveWidget::onResetClicked() {
    if (!m_layerCombo) return;
    const int layer = m_layerCombo->itemData(m_layerCombo->currentIndex()).toInt();
    emit curveResetRequested(layer);
}
