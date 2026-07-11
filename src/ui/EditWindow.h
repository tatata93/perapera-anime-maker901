#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <cstdint>
#include <optional>
#include <utility>

class QTableWidgetItem;
class QTableWidget;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QShowEvent;

namespace core {
class Project;
}

// 編集(カッティング)ウィンドウ(別ウィンドウ)。カットの並び替え・尺調整・進捗管理を行い、
// 右側で全カットを通しで再生プレビューできる。作画そのものはメインウィンドウで行うため、
// このウィンドウはカット構成の管理に専念する
class EditWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit EditWindow(QWidget* parent = nullptr);

    // プロジェクトの差し替え(新規/読込後)。所有権は持たない
    void setProject(core::Project* project);
    // カット一覧を再構築する(m_updatingガードで編集シグナルの暴発を防ぐ)。
    // カット構成が変わった可能性があるためフレームキャッシュも破棄する
    void refresh();

    // 通しプレビューの合成に使うキャンバスサイズ(MainWindowの作画キャンバスと同じ寸法を渡す)
    void setCanvasSize(int width, int height);

    // 動作確認用: 通しプレビューのグローバルコマ位置を直接指定する(再生はしない)
    void debugSeekToGlobalFrame(int globalFrame);

signals:
    void edited();            // カット名/尺/進捗またはカット順序が編集された
    void cutActivated(int index);  // カット一覧の行ダブルクリック(そのカットをアクティブにする)

protected:
    void showEvent(QShowEvent* event) override;

private:
    void onItemChanged(QTableWidgetItem* item);
    void moveSelectedCut(int delta);
    void updateTotalLabel();
    void updatePlaybackAvailability();

    // 全カット通しのグローバルコマ総数(24fps基準)
    qint64 totalFrames() const;
    // グローバルコマ位置を(カットindex, カット内コマ)に変換する。範囲外ならnullopt
    std::optional<std::pair<int, size_t>> globalFrameToCutKoma(qint64 globalFrame) const;
    // 指定カット/コマの合成画像をプレビュー解像度へ縮小したものを返す(キャッシュ利用、遅延生成)
    QPixmap framePixmap(int cutIndex, size_t koma);
    // 現在のm_globalFrameに応じてプレビュー画像・タイムラベル・スライダーを更新する
    void updatePreviewImage();

    void togglePlayback();
    void stopPlayback();
    void onPlaybackTick();

    core::Project* m_project = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_totalLabel = nullptr;

    // 通しプレビュー
    QLabel* m_previewLabel = nullptr;
    QPushButton* m_playButton = nullptr;
    QSlider* m_seekSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QTimer* m_playTimer = nullptr;
    bool m_playing = false;
    qint64 m_globalFrame = 0;  // 現在のプレビュー位置(全カット通しのコマ、0始まり)

    // フレームキャッシュ: キー=カットindex*100000+カット内コマ
    QHash<qint64, QPixmap> m_frameCache;

    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;

    bool m_updating = false;
};
