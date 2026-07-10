#pragma once

#include <QDockWidget>
#include <QImage>
#include <QStringList>

class QComboBox;
class QLabel;
class QResizeEvent;

// 参照ドック。設定ボード(キャラクター設定・美術設定などの資料)を作画中いつでも
// 参照できるようにする表示専用パネル。上部のコンボボックスでボードを選び、
// 選択中ボードの画像を白背景に合成してドック幅に合わせて表示する
// (アスペクト維持でスケーリング、resizeEventで再スケール)
class ReferencePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit ReferencePanel(QWidget* parent = nullptr);

    // ボード名一覧と選択中インデックスを反映する(シグナルは発火しない)
    void setBoards(const QStringList& names, int selectedIndex);
    // 選択中ボードの画像(透明下地のRGBA画像)を反映する。null画像なら「ボードなし」表示にする
    void setImage(const QImage& image);

signals:
    void boardSelected(int index);  // コンボボックスでボードが選ばれた(未選択解除時は-1)

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyScaledPixmap();

    QComboBox* m_combo = nullptr;
    QLabel* m_imageLabel = nullptr;
    QImage m_composedImage;  // 白背景合成済みの現在画像(nullなら未選択/空ボード)
    bool m_updating = false;
};
