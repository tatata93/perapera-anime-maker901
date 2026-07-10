#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVector4D>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Previz.h"
#include "previz/GltfLoader.h"

class QOpenGLShaderProgram;

// プリビズの3Dビューポート。glTFモデル群とグリッド床を物理カメラ(焦点距離/センサー幅)で描画する。
// 望遠のパース圧縮・広角の遠近強調は射影行列で自然に再現される。
// ナビゲーション: 右ドラッグ=見回し(カメラ回転)、中ドラッグ=平行移動、ホイール=前後ドリー。
// 操作はシーンのカメラ基本状態(state)を直接書き換え、cameraEdited()で通知する。
class PrevizViewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit PrevizViewport(QWidget* parent = nullptr);
    ~PrevizViewport() override;

    // 表示対象のシーン(所有権は持たない)と現在コマ
    void setScene(core::PrevizScene* scene);
    void setFrame(size_t frame);
    size_t frame() const { return m_frame; }

    // モデルファイルの読み込みキャッシュを破棄する(モデル削除・差し替え時)
    void clearMeshCache();

signals:
    void cameraEdited();  // ナビゲーション操作でカメラ基本状態が変わった

protected:
    void initializeGL() override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct GpuPrimitive {
        std::unique_ptr<QOpenGLBuffer> vbo;  // pos3+normal3
        std::unique_ptr<QOpenGLBuffer> ibo;
        int indexCount = 0;
        QVector4D color;
    };
    struct GpuMesh {
        std::vector<GpuPrimitive> primitives;
        bool loadFailed = false;
    };

    // コンテキストがカレントな状態で呼ぶこと
    GpuMesh* getOrLoadMesh(const std::string& filePath);
    void drawPrimitive(const GpuPrimitive& prim, const QMatrix4x4& model, const QMatrix4x4& viewProj, bool unlit);
    void buildGrid();
    void buildPlaceholderCube();

    QMatrix4x4 cameraView(const core::PrevizCameraState& state) const;
    QMatrix4x4 cameraProjection(size_t frame) const;

    core::PrevizScene* m_scene = nullptr;
    size_t m_frame = 0;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::map<std::string, GpuMesh> m_meshCache;
    GpuPrimitive m_grid;         // 床グリッド(ライン)
    GpuPrimitive m_placeholder;  // モデル未配置時の目安キューブ
    int m_gridVertexCount = 0;

    // ナビゲーション状態
    bool m_looking = false;
    bool m_panningView = false;
    QPointF m_lastMousePos;
};
