#pragma once

#include <QColor>
#include <QDockWidget>
#include <QImage>
#include <QList>
#include <QPair>
#include <QStringList>

class QComboBox;
class QLabel;
class QListWidget;
class QResizeEvent;

// 参照ドック。設定ボード(キャラクター設定・美術設定などの資料)を作画中いつでも
// 参照できるようにする表示専用パネル。上部のコンボボックスでボードを選び、
// 選択中ボードの画像を白背景に合成してドック幅に合わせて表示する
// (アスペクト維持でスケーリング、resizeEventで再スケール)。画像の下には選択中
// ボードの色指定(色指定書。「肌」「髪 影」などの名前付き色見本)を一覧表示する
class ReferencePanel : public QDockWidget {
    Q_OBJECT

public:
    explicit ReferencePanel(QWidget* parent = nullptr);

    // ボード名一覧と選択中インデックスを反映する(シグナルは発火しない)
    void setBoards(const QStringList& names, int selectedIndex);
    // 選択中ボードの画像(透明下地のRGBA画像)を反映する。null画像なら「ボードなし」表示にする
    void setImage(const QImage& image);
    // 選択中ボードの色指定(名前, 色)一覧を反映する
    void setColorSpecs(const QList<QPair<QString, QColor>>& specs);

signals:
    void boardSelected(int index);  // コンボボックスでボードが選ばれた(未選択解除時は-1)
    void colorPicked(QColor color);  // 色指定リストの行がクリックされ、その色が拾われた

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyScaledPixmap();

    QComboBox* m_combo = nullptr;
    QLabel* m_imageLabel = nullptr;
    QListWidget* m_colorSpecList = nullptr;  // 選択中ボードの色指定一覧: スウォッチ+名前
    QImage m_composedImage;  // 白背景合成済みの現在画像(nullなら未選択/空ボード)
    bool m_updating = false;
};
