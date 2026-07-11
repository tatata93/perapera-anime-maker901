#include "EditWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#include "core/Compositor.h"
#include "core/Project.h"

namespace {

constexpr double kFps = 24.0;  // タイムシートは24fps基準

// 通しプレビューの表示解像度
constexpr int kPreviewWidth = 480;
constexpr int kPreviewHeight = 270;

// フレームキャッシュのキー生成に使う「カットあたりの最大コマ数の目安」
// (実際の尺がこれを超えても衝突しないよう十分大きく取る)
constexpr qint64 kCacheKeyStride = 100000;

enum Column {
    kColNo = 0,
    kColName,
    kColDuration,
    kColSeconds,
    kColStatus,
    kColCount,
};

}  // namespace

EditWindow::EditWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("編集(カッティング) - perapera-anime-maker901"));
    resize(1100, 650);

    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);

    // 左: カット一覧テーブル+並べ替えボタン+合計尺ラベル
    auto* leftContainer = new QWidget(central);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(leftContainer);
    m_table->setColumnCount(kColCount);
    m_table->setHorizontalHeaderLabels({tr("No"), tr("カット名"), tr("尺コマ"), tr("秒"), tr("進捗")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setColumnWidth(kColName, 160);
    m_table->setColumnWidth(kColDuration, 70);
    m_table->setColumnWidth(kColSeconds, 70);
    m_table->setColumnWidth(kColStatus, 110);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_table);

    auto* buttonLayout = new QHBoxLayout();
    auto* upButton = new QPushButton(tr("上へ"), leftContainer);
    auto* downButton = new QPushButton(tr("下へ"), leftContainer);
    buttonLayout->addWidget(upButton);
    buttonLayout->addWidget(downButton);
    buttonLayout->addStretch();
    leftLayout->addLayout(buttonLayout);

    m_totalLabel = new QLabel(leftContainer);
    leftLayout->addWidget(m_totalLabel);

    mainLayout->addWidget(leftContainer, 3);

    // 右: 通しプレビュー(黒背景+アスペクト維持)+トランスポート
    auto* rightContainer = new QWidget(central);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_previewLabel = new QLabel(rightContainer);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(320, 180);
    m_previewLabel->setStyleSheet(QStringLiteral("background-color: black;"));
    rightLayout->addWidget(m_previewLabel, 1);

    auto* transportLayout = new QHBoxLayout();
    m_playButton = new QPushButton(tr("再生"), rightContainer);
    transportLayout->addWidget(m_playButton);
    m_seekSlider = new QSlider(Qt::Horizontal, rightContainer);
    transportLayout->addWidget(m_seekSlider, 1);
    rightLayout->addLayout(transportLayout);

    m_timeLabel = new QLabel(rightContainer);
    rightLayout->addWidget(m_timeLabel);

    mainLayout->addWidget(rightContainer, 4);

    setCentralWidget(central);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &EditWindow::onPlaybackTick);

    connect(m_table, &QTableWidget::itemChanged, this, &EditWindow::onItemChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0) emit cutActivated(row);
    });
    connect(upButton, &QPushButton::clicked, this, [this] { moveSelectedCut(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveSelectedCut(1); });

    connect(m_playButton, &QPushButton::clicked, this, &EditWindow::togglePlayback);
    connect(m_seekSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_updating) return;
        m_globalFrame = value;
        updatePreviewImage();
    });

    updateTotalLabel();
    updatePlaybackAvailability();
}

void EditWindow::setProject(core::Project* project) {
    m_project = project;
    m_globalFrame = 0;
    stopPlayback();
    m_frameCache.clear();
}

void EditWindow::setCanvasSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_canvasWidth = width;
    m_canvasHeight = height;
    m_frameCache.clear();
    updatePreviewImage();
}

void EditWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // 表示のたびにフレームキャッシュを破棄する(裏で編集された可能性があるため)
    m_frameCache.clear();
    updatePreviewImage();
}

void EditWindow::refresh() {
    if (!m_table) return;
    m_updating = true;
    m_frameCache.clear();  // カット構成が変わった可能性があるため古いキャッシュは破棄する

    if (!m_project || m_project->sceneCount() == 0) {
        m_table->setRowCount(0);
        m_updating = false;
        updateTotalLabel();
        updatePlaybackAvailability();
        updatePreviewImage();
        return;
    }

    core::Scene& scene = m_project->scene(0);
    const int count = static_cast<int>(scene.cutCount());
    m_table->setRowCount(count);

    for (int row = 0; row < count; ++row) {
        core::Cut& cut = scene.cut(static_cast<size_t>(row));

        // No(編集不可)
        auto* noItem = new QTableWidgetItem(QString::number(row + 1));
        noItem->setFlags(noItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColNo, noItem);

        // カット名(編集可)
        m_table->setItem(row, kColName, new QTableWidgetItem(QString::fromStdString(cut.name())));

        // 尺コマ(編集可、数値)
        m_table->setItem(row, kColDuration, new QTableWidgetItem(QString::number(cut.frameCount())));

        // 秒(表示のみ、24fps換算)
        const double seconds = static_cast<double>(cut.frameCount()) / kFps;
        auto* secItem = new QTableWidgetItem(QStringLiteral("%1s").arg(seconds, 0, 'f', 2));
        secItem->setFlags(secItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, kColSeconds, secItem);

        // 進捗(コンボボックス)
        auto* statusCombo = new QComboBox(m_table);
        statusCombo->addItems(
            {tr("未着手"), tr("レイアウト"), tr("原画"), tr("動画"), tr("仕上げ"), tr("撮影"), tr("完了")});
        statusCombo->setCurrentIndex(static_cast<int>(cut.status()));
        connect(statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row](int index) {
            if (m_updating || !m_project || m_project->sceneCount() == 0) return;
            core::Scene& s = m_project->scene(0);
            if (row < 0 || static_cast<size_t>(row) >= s.cutCount()) return;
            s.cut(static_cast<size_t>(row)).setStatus(static_cast<core::CutStatus>(index));
            emit edited();
        });
        m_table->setCellWidget(row, kColStatus, statusCombo);
    }

    m_updating = false;
    updateTotalLabel();
    updatePlaybackAvailability();
    updatePreviewImage();
}

void EditWindow::onItemChanged(QTableWidgetItem* item) {
    if (m_updating || !m_project || !item) return;
    if (m_project->sceneCount() == 0) return;
    core::Scene& scene = m_project->scene(0);
    const int row = item->row();
    if (row < 0 || static_cast<size_t>(row) >= scene.cutCount()) return;
    core::Cut& cut = scene.cut(static_cast<size_t>(row));

    switch (item->column()) {
        case kColName:
            cut.setName(item->text().toStdString());
            emit edited();
            break;
        case kColDuration: {
            bool ok = false;
            const int value = item->text().toInt(&ok);
            if (!ok || value <= 0) {
                // 無効入力は元に戻す
                m_updating = true;
                item->setText(QString::number(cut.frameCount()));
                m_updating = false;
                return;
            }
            cut.setFrameCount(static_cast<size_t>(value));

            m_updating = true;
            if (QTableWidgetItem* secItem = m_table->item(row, kColSeconds)) {
                const double seconds = static_cast<double>(cut.frameCount()) / kFps;
                secItem->setText(QStringLiteral("%1s").arg(seconds, 0, 'f', 2));
            }
            m_updating = false;

            // 尺変更でグローバルコマと各カットの対応がずれるため、キャッシュを破棄する
            m_frameCache.clear();
            updateTotalLabel();
            updatePlaybackAvailability();
            updatePreviewImage();
            emit edited();
            break;
        }
        default:
            break;  // No/秒/進捗はこの経路では編集不可
    }
}

void EditWindow::moveSelectedCut(int delta) {
    if (!m_project || m_project->sceneCount() == 0) return;
    core::Scene& scene = m_project->scene(0);
    const int row = m_table->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || static_cast<size_t>(target) >= scene.cutCount()) return;

    scene.moveCut(static_cast<size_t>(row), static_cast<size_t>(target));
    refresh();
    m_table->setCurrentCell(target, kColName);
    emit edited();
}

void EditWindow::updateTotalLabel() {
    if (!m_totalLabel) return;
    const qint64 total = totalFrames();
    const double seconds = static_cast<double>(total) / kFps;
    m_totalLabel->setText(tr(" 合計: %1コマ (%2秒) ").arg(total).arg(seconds, 0, 'f', 2));
    if (m_seekSlider) {
        const QSignalBlocker blocker(m_seekSlider);
        m_seekSlider->setRange(0, static_cast<int>(std::max<qint64>(0, total - 1)));
    }
}

void EditWindow::updatePlaybackAvailability() {
    const qint64 total = totalFrames();
    const bool enabled = total > 0;
    if (m_playButton) m_playButton->setEnabled(enabled);
    if (m_seekSlider) m_seekSlider->setEnabled(enabled);
    if (!enabled) stopPlayback();
}

qint64 EditWindow::totalFrames() const {
    if (!m_project || m_project->sceneCount() == 0) return 0;
    core::Scene& scene = m_project->scene(0);
    qint64 total = 0;
    for (size_t ci = 0; ci < scene.cutCount(); ++ci) total += static_cast<qint64>(scene.cut(ci).frameCount());
    return total;
}

std::optional<std::pair<int, size_t>> EditWindow::globalFrameToCutKoma(qint64 globalFrame) const {
    if (!m_project || m_project->sceneCount() == 0 || globalFrame < 0) return std::nullopt;
    core::Scene& scene = m_project->scene(0);
    qint64 remaining = globalFrame;
    for (size_t ci = 0; ci < scene.cutCount(); ++ci) {
        const qint64 count = static_cast<qint64>(scene.cut(ci).frameCount());
        if (remaining < count) return std::make_pair(static_cast<int>(ci), static_cast<size_t>(remaining));
        remaining -= count;
    }
    return std::nullopt;  // 範囲外
}

QPixmap EditWindow::framePixmap(int cutIndex, size_t koma) {
    const qint64 key = static_cast<qint64>(cutIndex) * kCacheKeyStride + static_cast<qint64>(koma);
    const auto it = m_frameCache.constFind(key);
    if (it != m_frameCache.constEnd()) return it.value();

    core::Scene& scene = m_project->scene(0);
    const core::Cut& cut = scene.cut(static_cast<size_t>(cutIndex));
    const core::Bitmap bitmap = core::renderCutFrame(cut, koma, m_canvasWidth, m_canvasHeight);
    const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    // scaled()はピクセルデータを新規複製するため、bitmap(ローカル変数)の寿命中に呼べば安全
    const QPixmap pixmap = QPixmap::fromImage(
        image.scaled(kPreviewWidth, kPreviewHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_frameCache.insert(key, pixmap);
    return pixmap;
}

void EditWindow::updatePreviewImage() {
    if (!m_previewLabel || !m_timeLabel) return;

    const qint64 total = totalFrames();
    if (total <= 0) {
        m_previewLabel->setPixmap(QPixmap());
        m_timeLabel->setText(tr("カットなし"));
        return;
    }

    m_globalFrame = std::clamp<qint64>(m_globalFrame, 0, total - 1);
    const auto cutKoma = globalFrameToCutKoma(m_globalFrame);
    if (!cutKoma) {
        m_previewLabel->setPixmap(QPixmap());
        m_timeLabel->setText(tr("カットなし"));
        return;
    }
    const int cutIndex = cutKoma->first;
    const size_t koma = cutKoma->second;

    m_previewLabel->setPixmap(framePixmap(cutIndex, koma));

    const double elapsedSec = static_cast<double>(m_globalFrame) / kFps;
    const double totalSec = static_cast<double>(total) / kFps;
    m_timeLabel->setText(tr("カット%1 コマ%2 / 全体 %3/%4 (%5s/%6s)")
                              .arg(cutIndex + 1)
                              .arg(koma + 1)
                              .arg(m_globalFrame + 1)
                              .arg(total)
                              .arg(elapsedSec, 0, 'f', 1)
                              .arg(totalSec, 0, 'f', 1));

    if (m_seekSlider) {
        const QSignalBlocker blocker(m_seekSlider);
        m_seekSlider->setValue(static_cast<int>(m_globalFrame));
    }
}

void EditWindow::togglePlayback() {
    if (m_playing) {
        stopPlayback();
        return;
    }
    const qint64 total = totalFrames();
    if (total <= 0) return;
    if (m_globalFrame >= total - 1) m_globalFrame = 0;  // 末尾で押されたら先頭から再生し直す
    m_playing = true;
    m_playButton->setText(tr("一時停止"));
    m_playTimer->start(static_cast<int>(1000.0 / kFps));
}

void EditWindow::stopPlayback() {
    m_playing = false;
    if (m_playTimer) m_playTimer->stop();
    if (m_playButton) m_playButton->setText(tr("再生"));
}

void EditWindow::onPlaybackTick() {
    const qint64 total = totalFrames();
    if (total <= 0) {
        stopPlayback();
        return;
    }
    if (m_globalFrame + 1 >= total) {
        m_globalFrame = total - 1;
        stopPlayback();  // 末尾まで行ったら停止(ループしない)
        updatePreviewImage();
        return;
    }
    ++m_globalFrame;
    updatePreviewImage();
}

void EditWindow::debugSeekToGlobalFrame(int globalFrame) {
    m_globalFrame = std::max(0, globalFrame);
    updatePreviewImage();
}
