#pragma once

#include <QDockWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

// カメラフレーム(画面に写る範囲)を編集するドッキングパネル。
// 中心X/中心Y/スケール%のスピンと、キー追加/削除/全削除ボタン、枠表示チェックを持つ。
// レイアウト工程で「画面に写る範囲」をカットごとに指定し、キーで動かす(PAN/T.U.の基盤)機能のUI。
class CameraPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit CameraPanel(QWidget* parent = nullptr);

    // スピンの値を設定する(シグナルは発火しない)。中心はキャンバスpx、スケールは%(5〜400)
    void setValues(double centerX, double centerY, double scalePercent);
    // 現在コマのキー有無ラベルを更新する。
    // hasKeyOnFrame=true: 「キー: あり」、false かつhasAnyKeys=true: 「キー: なし(補間)」、
    // 両方false: 「キー: なし」
    void setKeyState(bool hasKeyOnFrame, bool hasAnyKeys);

    double centerX() const;
    double centerY() const;
    double scalePercent() const;
    bool showFrameEnabled() const;

signals:
    void valuesChanged(double centerX, double centerY, double scalePercent);  // スピン編集(プレビュー用)
    void addKeyRequested();         // 「キー追加」ボタン(現在コマに現在スピン値でキーを打つ)
    void removeKeyRequested();      // 「キー削除」ボタン(現在コマのキーを消す)
    void clearAllKeysRequested();   // 「全キー削除」ボタン
    void showFrameToggled(bool enabled);  // 「枠を表示」チェック

private:
    QDoubleSpinBox* m_centerXSpin = nullptr;
    QDoubleSpinBox* m_centerYSpin = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QCheckBox* m_showFrameCheck = nullptr;
    QLabel* m_keyStateLabel = nullptr;
    bool m_updating = false;
};
