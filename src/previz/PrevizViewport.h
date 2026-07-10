#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVector3D>
#include <QVector4D>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Previz.h"
#include "previz/GltfLoader.h"

class QOpenGLShaderProgram;

// プリビズの3Dビューポート。glTFモデル群とグリッド床を描画する。
//
// 視点は2モード(MMD/Blender方式):
// - 作業視点(既定): 自由なオービットカメラでシーンを編集する。本番カメラはギズモ(錐台)で表示
// - カメラ視点: 本番カメラ(物理カメラ: 焦点距離/センサー幅→画角)から見る。下敷きにはこの絵を使う
//
// 作業視点の操作: 右ドラッグ=軌道回転 / 中ドラッグ=注視点パン / ホイール=距離 /
//                左ドラッグ=選択モデルを床面で移動(Shift+ドラッグ=上下)
// カメラ視点の操作: 右ドラッグ=見回し / 中ドラッグ=平行移動 / ホイール=前後ドリー
// (カメラ視点の操作はキーが無ければ基本状態、あれば現在コマのキーを編集する)
class PrevizViewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class ViewMode { Work, Camera };

    explicit PrevizViewport(QWidget* parent = nullptr);
    ~PrevizViewport() override;

    // 表示対象のシーン(所有権は持たない)と現在コマ
    void setScene(core::PrevizScene* scene);
    void setFrame(size_t frame);
    size_t frame() const { return m_frame; }

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }

    // ドラッグ移動の対象モデル(モデル一覧の選択。-1=なし)
    void setSelectedModel(int index);

    // 下敷き用: 表示モードに関係なくカメラ視点で描画した画像を返す
    QImage renderCameraViewImage();

    // モデルファイルの読み込みキャッシュを破棄する(モデル削除・差し替え時)
    void clearMeshCache();

signals:
    void cameraEdited();  // カメラ視点操作でカメラが変わった
    void modelEdited();   // ドラッグ移動でモデル配置が変わった

protected:
    void initializeGL() override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct GpuPrimitive {
        std::unique_ptr<QOpenGLBuffer> vbo;  // pos3+normal3
        std::unique_ptr<QOpenGLBuffer> ibo;
        int indexCount = 0;
        int lineVertexCount = 0;  // ibo無しの場合のGL_LINES頂点数
        QVector4D color;
    };
    struct GpuMesh {
        std::vector<GpuPrimitive> primitives;
        bool loadFailed = false;
    };

    bool usingCameraView() const { return m_forceCameraView || m_viewMode == ViewMode::Camera; }
    QMatrix4x4 currentView() const;
    QMatrix4x4 currentProjection() const;
    QMatrix4x4 cameraWorldMatrix(const core::PrevizCameraState& state) const;

    core::PrevizCameraState& editableCameraState();
    core::PrevizTransform editableModelTransform(core::PrevizModel& model) const;
    void writeModelTransform(core::PrevizModel& model, const core::PrevizTransform& tf) const;

    // マウス位置からy=planeYの水平面との交点を求める(モデルのドラッグ移動用)
    bool groundHit(QPointF mousePos, float planeY, QVector3D& out) const;

    // コンテキストがカレントな状態で呼ぶこと
    GpuMesh* getOrLoadMesh(const std::string& filePath);
    void drawPrimitive(const GpuPrimitive& prim, const QMatrix4x4& model, const QMatrix4x4& viewProj, bool unlit,
                       bool highlight = false);
    void buildGrid();
    void buildPlaceholderCube();
    void buildCameraGizmo();

    QMatrix4x4 cameraView(const core::PrevizCameraState& state) const;
    QMatrix4x4 cameraProjection(size_t frame, float aspect) const;
    // シーン一式(グリッド/モデル/ギズモ)を指定のビュー射影で描く(paintGLとオフスクリーンで共用)
    void renderScene(const QMatrix4x4& viewProj);
    // FPS移動: ローカル方向(x=右, y=上, z=前)へ視点を動かす
    void moveFreely(const QVector3D& localDir, float step);

    core::PrevizScene* m_scene = nullptr;
    size_t m_frame = 0;
    ViewMode m_viewMode = ViewMode::Work;
    bool m_forceCameraView = false;
    int m_selectedModel = -1;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::map<std::string, GpuMesh> m_meshCache;
    GpuPrimitive m_grid;         // 床グリッド(ライン)
    GpuPrimitive m_placeholder;  // モデル未配置時の目安キューブ
    GpuPrimitive m_cameraGizmo;  // 本番カメラのギズモ(錐台ライン)

    // 作業視点(オービットカメラ)
    float m_orbitYaw = 35.0f;
    float m_orbitPitch = -20.0f;
    float m_orbitDistance = 8.0f;
    QVector3D m_orbitTarget{0.0f, 0.5f, 0.0f};

    // ナビゲーション状態
    bool m_looking = false;
    bool m_panningView = false;
    bool m_draggingModel = false;
    float m_dragPlaneY = 0.0f;
    QVector3D m_dragLastHit;
    QPointF m_lastMousePos;
};
