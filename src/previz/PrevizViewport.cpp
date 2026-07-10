#include "PrevizViewport.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <cmath>

namespace {

const char* kVertexShader = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
varying vec3 vNormal;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
}
)";

const char* kFragmentShader = R"(
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform float uUnlit;
varying vec3 vNormal;
void main() {
    if (uUnlit > 0.5) {
        gl_FragColor = uColor;
        return;
    }
    float d = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    gl_FragColor = vec4(uColor.rgb * (0.35 + 0.65 * d), uColor.a);
}
)";

}  // namespace

PrevizViewport::PrevizViewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(480, 270);
}

PrevizViewport::~PrevizViewport() {
    makeCurrent();
    m_meshCache.clear();
    m_grid = {};
    m_placeholder = {};
    m_program.reset();
    doneCurrent();
}

void PrevizViewport::setScene(core::PrevizScene* scene) {
    m_scene = scene;
    update();
}

void PrevizViewport::setFrame(size_t frame) {
    m_frame = frame;
    update();
}

void PrevizViewport::clearMeshCache() {
    makeCurrent();
    m_meshCache.clear();
    doneCurrent();
    update();
}

void PrevizViewport::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aNormal", 1);
    m_program->link();

    buildGrid();
    buildPlaceholderCube();
}

void PrevizViewport::buildGrid() {
    // 床グリッド(20m四方、1m間隔)のライン頂点(pos+ダミー法線)
    std::vector<float> lines;
    const float half = 10.0f;
    for (int i = -10; i <= 10; ++i) {
        const float p = static_cast<float>(i);
        lines.insert(lines.end(), {p, 0, -half, 0, 1, 0, p, 0, half, 0, 1, 0});
        lines.insert(lines.end(), {-half, 0, p, 0, 1, 0, half, 0, p, 0, 1, 0});
    }
    m_gridVertexCount = static_cast<int>(lines.size() / 6);

    m_grid.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_grid.vbo->create();
    m_grid.vbo->bind();
    m_grid.vbo->allocate(lines.data(), static_cast<int>(lines.size() * sizeof(float)));
    m_grid.vbo->release();
    m_grid.color = QVector4D(0.45f, 0.47f, 0.52f, 1.0f);
}

void PrevizViewport::buildPlaceholderCube() {
    // モデル未配置時の目安: 1m角キューブ(原点、床の上)
    const float v[] = {// 前面(+z)
                       -0.5f, 0, 0.5f, 0, 0, 1, 0.5f, 0, 0.5f, 0, 0, 1, 0.5f, 1, 0.5f, 0, 0, 1, -0.5f, 1, 0.5f, 0, 0, 1,
                       // 背面(-z)
                       -0.5f, 0, -0.5f, 0, 0, -1, 0.5f, 0, -0.5f, 0, 0, -1, 0.5f, 1, -0.5f, 0, 0, -1, -0.5f, 1, -0.5f, 0, 0, -1,
                       // 左(-x)
                       -0.5f, 0, -0.5f, -1, 0, 0, -0.5f, 0, 0.5f, -1, 0, 0, -0.5f, 1, 0.5f, -1, 0, 0, -0.5f, 1, -0.5f, -1, 0, 0,
                       // 右(+x)
                       0.5f, 0, -0.5f, 1, 0, 0, 0.5f, 0, 0.5f, 1, 0, 0, 0.5f, 1, 0.5f, 1, 0, 0, 0.5f, 1, -0.5f, 1, 0, 0,
                       // 上(+y)
                       -0.5f, 1, -0.5f, 0, 1, 0, 0.5f, 1, -0.5f, 0, 1, 0, 0.5f, 1, 0.5f, 0, 1, 0, -0.5f, 1, 0.5f, 0, 1, 0,
                       // 下(-y)
                       -0.5f, 0, -0.5f, 0, -1, 0, 0.5f, 0, -0.5f, 0, -1, 0, 0.5f, 0, 0.5f, 0, -1, 0, -0.5f, 0, 0.5f, 0, -1, 0};
    const uint32_t idx[] = {0,  1,  2,  0,  2,  3,  4,  6,  5,  4,  7,  6,  8,  9,  10, 8,  10, 11,
                            12, 14, 13, 12, 15, 14, 16, 17, 18, 16, 18, 19, 20, 22, 21, 20, 23, 22};

    m_placeholder.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_placeholder.vbo->create();
    m_placeholder.vbo->bind();
    m_placeholder.vbo->allocate(v, sizeof(v));
    m_placeholder.vbo->release();
    m_placeholder.ibo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
    m_placeholder.ibo->create();
    m_placeholder.ibo->bind();
    m_placeholder.ibo->allocate(idx, sizeof(idx));
    m_placeholder.ibo->release();
    m_placeholder.indexCount = 36;
    m_placeholder.color = QVector4D(0.55f, 0.65f, 0.85f, 1.0f);
}

PrevizViewport::GpuMesh* PrevizViewport::getOrLoadMesh(const std::string& filePath) {
    auto it = m_meshCache.find(filePath);
    if (it != m_meshCache.end()) return &it->second;

    GpuMesh mesh;
    previz::MeshData data;
    std::string error;
    if (!previz::loadGltfMesh(filePath, data, &error)) {
        mesh.loadFailed = true;  // 失敗を記録して毎フレーム再試行しない
    } else {
        for (const auto& src : data.primitives) {
            GpuPrimitive prim;
            prim.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
            prim.vbo->create();
            prim.vbo->bind();
            prim.vbo->allocate(src.vertices.data(), static_cast<int>(src.vertices.size() * sizeof(float)));
            prim.vbo->release();
            prim.ibo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
            prim.ibo->create();
            prim.ibo->bind();
            prim.ibo->allocate(src.indices.data(), static_cast<int>(src.indices.size() * sizeof(uint32_t)));
            prim.ibo->release();
            prim.indexCount = static_cast<int>(src.indices.size());
            prim.color = QVector4D(src.color[0], src.color[1], src.color[2], src.color[3]);
            mesh.primitives.push_back(std::move(prim));
        }
    }
    auto [inserted, _] = m_meshCache.emplace(filePath, std::move(mesh));
    return &inserted->second;
}

QMatrix4x4 PrevizViewport::cameraView(const core::PrevizCameraState& state) const {
    // カメラのワールド行列 = T * Ry(ヨー) * Rx(ピッチ) * Rz(ロール)。ビューはその逆行列
    QMatrix4x4 world;
    world.translate(state.position.x, state.position.y, state.position.z);
    world.rotate(state.rotationDeg.y, 0, 1, 0);
    world.rotate(state.rotationDeg.x, 1, 0, 0);
    world.rotate(state.rotationDeg.z, 0, 0, 1);
    return world.inverted();
}

QMatrix4x4 PrevizViewport::cameraProjection(size_t frame) const {
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 16.0f / 9.0f;
    // 水平画角(物理カメラ)→垂直画角へ変換して射影を作る
    const float hfovRad = m_scene ? m_scene->camera.horizontalFovDeg(frame) * 3.14159265f / 180.0f : 0.69f;
    const float vfovDeg = 2.0f * std::atan(std::tan(hfovRad * 0.5f) / aspect) * 180.0f / 3.14159265f;
    QMatrix4x4 proj;
    proj.perspective(vfovDeg, aspect, 0.05f, 2000.0f);
    return proj;
}

void PrevizViewport::drawPrimitive(const GpuPrimitive& prim, const QMatrix4x4& model, const QMatrix4x4& viewProj,
                                   bool unlit) {
    m_program->setUniformValue("uMvp", viewProj * model);
    m_program->setUniformValue("uModel", model);
    m_program->setUniformValue("uColor", prim.color);
    m_program->setUniformValue("uUnlit", unlit ? 1.0f : 0.0f);

    prim.vbo->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));

    if (prim.ibo) {
        prim.ibo->bind();
        glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, nullptr);
        prim.ibo->release();
    } else {
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
    }
    prim.vbo->release();
}

void PrevizViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    if (!m_program) return;

    const core::PrevizCameraState camState =
        m_scene ? m_scene->camera.stateAt(m_frame) : core::PrevizCameraState{};
    const QMatrix4x4 viewProj = cameraProjection(m_frame) * cameraView(camState);

    m_program->bind();
    m_program->setUniformValue("uLightDir", QVector3D(0.4f, 1.0f, 0.6f));

    // 床グリッド
    drawPrimitive(m_grid, QMatrix4x4(), viewProj, /*unlit=*/true);

    // モデル群(未配置なら目安キューブ)
    bool drewAny = false;
    if (m_scene) {
        for (const core::PrevizModel& model : m_scene->models) {
            GpuMesh* mesh = getOrLoadMesh(model.filePath);
            if (!mesh || mesh->loadFailed) continue;

            const core::PrevizTransform tf = model.transformAt(m_frame);
            QMatrix4x4 m;
            m.translate(tf.position.x, tf.position.y, tf.position.z);
            m.rotate(tf.rotationDeg.y, 0, 1, 0);
            m.rotate(tf.rotationDeg.x, 1, 0, 0);
            m.rotate(tf.rotationDeg.z, 0, 0, 1);
            m.scale(tf.scale.x, tf.scale.y, tf.scale.z);

            for (const GpuPrimitive& prim : mesh->primitives) {
                drawPrimitive(prim, m, viewProj, /*unlit=*/false);
                drewAny = true;
            }
        }
    }
    if (!drewAny) {
        drawPrimitive(m_placeholder, QMatrix4x4(), viewProj, /*unlit=*/false);
    }

    m_program->release();
}

void PrevizViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::RightButton) m_looking = true;
    if (event->button() == Qt::MiddleButton) m_panningView = true;
}

void PrevizViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!m_scene || (!m_looking && !m_panningView)) return;
    const QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();
    core::PrevizCameraState& cam = m_scene->camera.state;

    if (m_looking) {
        // 見回し: ヨー/ピッチを回す
        cam.rotationDeg.y -= static_cast<float>(delta.x()) * 0.3f;
        cam.rotationDeg.x -= static_cast<float>(delta.y()) * 0.3f;
        cam.rotationDeg.x = std::clamp(cam.rotationDeg.x, -89.0f, 89.0f);
    } else if (m_panningView) {
        // 平行移動: カメラのローカル右/上方向へ動かす
        QMatrix4x4 rot;
        rot.rotate(cam.rotationDeg.y, 0, 1, 0);
        rot.rotate(cam.rotationDeg.x, 1, 0, 0);
        const QVector3D right = rot.map(QVector3D(1, 0, 0));
        const QVector3D up = rot.map(QVector3D(0, 1, 0));
        const float scale = 0.01f;
        const QVector3D move = right * static_cast<float>(-delta.x()) * scale + up * static_cast<float>(delta.y()) * scale;
        cam.position.x += move.x();
        cam.position.y += move.y();
        cam.position.z += move.z();
    }
    emit cameraEdited();
    update();
}

void PrevizViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) m_looking = false;
    if (event->button() == Qt::MiddleButton) m_panningView = false;
}

void PrevizViewport::wheelEvent(QWheelEvent* event) {
    if (!m_scene) return;
    core::PrevizCameraState& cam = m_scene->camera.state;
    // 前後ドリー: カメラの前方向へ移動
    QMatrix4x4 rot;
    rot.rotate(cam.rotationDeg.y, 0, 1, 0);
    rot.rotate(cam.rotationDeg.x, 1, 0, 0);
    const QVector3D forward = rot.map(QVector3D(0, 0, -1));
    const float amount = event->angleDelta().y() > 0 ? 0.5f : -0.5f;
    cam.position.x += forward.x() * amount;
    cam.position.y += forward.y() * amount;
    cam.position.z += forward.z() * amount;
    emit cameraEdited();
    update();
    event->accept();
}
