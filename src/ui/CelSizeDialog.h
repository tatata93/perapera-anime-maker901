#pragma once

#include <QDialog>

class QSpinBox;

// セルの用紙サイズ(引きセル)を指定するダイアログ。
// 背景セルなどをキャンバスより大きい紙にして、位置キーでタップ位置をずらすことで
// パン(引き)を再現できるようにする。幅/高さをスピンで直接指定するほか、
// よく使う倍率をプリセットボタンで即座に反映できる。
class CelSizeDialog : public QDialog {
    Q_OBJECT

public:
    // currentW/currentH: 現在の用紙サイズ(0ならキャンバスサイズが使われている状態として扱う)。
    // canvasW/canvasH: プリセットボタンの基準になるキャンバスサイズ
    CelSizeDialog(int currentW, int currentH, int canvasW, int canvasH, QWidget* parent = nullptr);

    // QWidget::width()/height()(ウィジェット自身の画面サイズ)との混同を避けるため
    // paperWidth()/paperHeight()という名前にする
    int paperWidth() const;
    int paperHeight() const;

private:
    int m_canvasWidth;
    int m_canvasHeight;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
};
