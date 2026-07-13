#pragma once

#include <QWidget>

class QComboBox;
class QPushButton;

// フィルムエフェクトの層別(R/G/B)分光応答カーブをグラフ表示・ドラッグ編集する複合ウィジェット。
// 横軸=入力光の強さ(0〜1、5点固定間隔0,0.25,0.5,0.75,1.0)、縦軸=その層が記録する量(0〜1)。
// 5点を区分線形補間したカーブを層(R/G/B)ごとに描き、制御点(丸)をドラッグして縦方向だけ編集できる。
// 「編集する層」コンボで前面に出す(≒ドラッグ対象にする)層を選び、「リセット」で選択層
// (コンボが「全て」なら全層)を恒等カーブへ戻す。
class FilmCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit FilmCurveWidget(QWidget* parent = nullptr);

    // layer: 0=R, 1=G, 2=B。コマ移動時など外部から現在値を反映するために使う。
    // 反映中はm_updatingで内部のドラッグ確定シグナルが暴発しないようガードする
    void setPoints(int layer, const double pts[5]);

    // 内部のグラフ描画・ドラッグ処理を担う子ウィジェット(GraphArea)からの通知専用。
    // GraphArea以外から呼ばないこと
    void reportPointDrag(int layer, int pointIndex, double value);

signals:
    // ドラッグ中/確定のたびに発行される。layer=0(R)/1(G)/2(B)、pointIndex=0..4、value=0..1
    void curveChanged(int layer, int pointIndex, double value);
    // リセットボタン押下。layer=-1は全層、0..2は選択層のみ
    void curveResetRequested(int layer);

private:
    class GraphArea;  // グラフ描画+ドラッグ処理する内部ウィジェット(.cppで定義)

    void onLayerComboChanged(int index);
    void onResetClicked();

    QComboBox* m_layerCombo = nullptr;
    GraphArea* m_graph = nullptr;
    bool m_updating = false;  // setPoints()で外部反映中はreportPointDragからのシグナル発行を止める
};
