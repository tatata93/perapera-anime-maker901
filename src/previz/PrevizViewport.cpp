#include "PrevizViewport.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "previz/StlLoader.h"

namespace {

// パスの拡張子(小文字化)を返す。ドットは含まない
std::string lowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

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

const QVector4D kGizmoColor(1.0f, 0.6f, 0.2f, 1.0f);      // 本番カメラのギズモ(オレンジ)
const QVector4D kHighlightColor(1.0f, 0.65f, 0.25f, 1.0f);  // 選択モデルの強調色

}  // namespace

PrevizViewport::PrevizViewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(480, 270);
    setFocusPolicy(Qt::ClickFocus);  // クリックでフォーカスを取り、WASDキー移動を受け付ける
}

PrevizViewport::~PrevizViewport() {
    makeCurrent();
    m_meshCache.clear();
    m_grid = {};
    m_placeholder = {};
    m_cameraGizmo = {};
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

void PrevizViewport::setViewMode(ViewMode mode) {
    m_viewMode = mode;
    update();
}

void PrevizViewport::setSelectedModel(int index) {
    m_selectedModel = index;
    update();
}

QImage PrevizViewport::renderCameraViewImage(float aspectWOverH) {
    // 表示モードに関係なく、下敷きには本番カメラの絵を使う。
    // ウィンドウサイズに依存しないオフスクリーンFBOで、呼び出し側が指定したアスペクト比
    // (既定16:9、通常はMainWindowのキャンバスサイズの比率を渡す)で描くことで、
    // 作品との比率ずれによる「つぶれ」を防ぐ。プロジェクション行列のアスペクトもkWidth/kHeight
    // からそのまま算出されるため、3D投影自体もキャンバスのアスペクトへ追従する
    constexpr int kWidth = 960;
    const float safeAspect = aspectWOverH > 0.0f ? aspectWOverH : 16.0f / 9.0f;
    const int kHeight = std::max(1, static_cast<int>(std::lround(kWidth / safeAspect)));

    makeCurrent();
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QOpenGLFramebufferObject fbo(kWidth, kHeight, format);
    fbo.bind();
    glViewport(0, 0, kWidth, kHeight);
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    m_forceCameraView = true;
    const core::PrevizCameraState state = m_scene ? m_scene->camera.stateAt(m_frame) : core::PrevizCameraState{};
    const QMatrix4x4 viewProj =
        cameraProjection(m_frame, static_cast<float>(kWidth) / static_cast<float>(kHeight)) * cameraView(state);
    renderScene(viewProj);
    m_forceCameraView = false;

    fbo.release();
    QImage image = fbo.toImage();
    doneCurrent();
    update();
    return image;
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
    buildCameraGizmo();
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
    m_grid.lineVertexCount = static_cast<int>(lines.size() / 6);

    m_grid.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_grid.vbo->create();
    m_grid.vbo->bind();
    m_grid.vbo->allocate(lines.data(), static_cast<int>(lines.size() * sizeof(float)));
    m_grid.vbo->release();
    m_grid.color = QVector4D(0.45f, 0.47f, 0.52f, 1.0f);
}

void PrevizViewport::buildCameraGizmo() {
    // 本番カメラの錐台ギズモ(カメラローカル空間、前方=-Z)。上方向の目印三角付き
    const float z = -0.7f, x = 0.35f, y = 0.22f;
    const float apex[3] = {0, 0, 0};
    const float c[4][3] = {{-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z}};
    std::vector<float> lines;
    const auto addLine = [&lines](const float* a, const float* b) {
        lines.insert(lines.end(), {a[0], a[1], a[2], 0, 1, 0, b[0], b[1], b[2], 0, 1, 0});
    };
    for (int i = 0; i < 4; ++i) {
        addLine(apex, c[i]);
        addLine(c[i], c[(i + 1) % 4]);
    }
    // 上向き三角(カメラの天地の目印)
    const float t0[3] = {-0.12f, y, z}, t1[3] = {0.12f, y, z}, t2[3] = {0, y + 0.15f, z};
    addLine(t0, t1);
    addLine(t1, t2);
    addLine(t2, t0);

    m_cameraGizmo.lineVertexCount = static_cast<int>(lines.size() / 6);
    m_cameraGizmo.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_cameraGizmo.vbo->create();
    m_cameraGizmo.vbo->bind();
    m_cameraGizmo.vbo->allocate(lines.data(), static_cast<int>(lines.size() * sizeof(float)));
    m_cameraGizmo.vbo->release();
    m_cameraGizmo.color = kGizmoColor;
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
    if (filePath == ":box") {
        // 組み込みプリミティブ: 1m角の箱(レイアウトのブロッキング用)
        data = previz::makeBoxMeshData();
    } else if (filePath == ":cylinder") {
        // 組み込みプリミティブ: 円柱(半径0.5・高さ1)
        data = previz::makeCylinderMeshData();
    } else if (filePath == ":sphere") {
        // 組み込みプリミティブ: 球(半径0.5)
        data = previz::makeSphereMeshData();
    } else if (lowerExtension(filePath) == "stl") {
        if (!previz::loadStlMesh(filePath, data, &error)) mesh.loadFailed = true;
    } else if (!previz::loadGltfMesh(filePath, data, &error)) {
        mesh.loadFailed = true;  // 失敗を記録して毎フレーム再試行しない
    }
    if (!mesh.loadFailed) {
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

QMatrix4x4 PrevizViewport::cameraWorldMatrix(const core::PrevizCameraState& state) const {
    QMatrix4x4 world;
    world.translate(state.position.x, state.position.y, state.position.z);
    world.rotate(state.rotationDeg.y, 0, 1, 0);
    world.rotate(state.rotationDeg.x, 1, 0, 0);
    world.rotate(state.rotationDeg.z, 0, 0, 1);
    return world;
}

QMatrix4x4 PrevizViewport::cameraView(const core::PrevizCameraState& state) const {
    return cameraWorldMatrix(state).inverted();
}

QMatrix4x4 PrevizViewport::cameraProjection(size_t frame, float aspect) const {
    // 水平画角(物理カメラ)→垂直画角へ変換して射影を作る
    const float hfovRad = m_scene ? m_scene->camera.horizontalFovDeg(frame) * 3.14159265f / 180.0f : 0.69f;
    const float vfovDeg = 2.0f * std::atan(std::tan(hfovRad * 0.5f) / aspect) * 180.0f / 3.14159265f;
    QMatrix4x4 proj;
    proj.perspective(vfovDeg, aspect, 0.05f, 2000.0f);
    return proj;
}

QMatrix4x4 PrevizViewport::currentView() const {
    if (usingCameraView()) {
        const core::PrevizCameraState state =
            m_scene ? m_scene->camera.stateAt(m_frame) : core::PrevizCameraState{};
        return cameraView(state);
    }
    // 作業視点: 注視点まわりのオービット
    QMatrix4x4 rot;
    rot.rotate(m_orbitYaw, 0, 1, 0);
    rot.rotate(m_orbitPitch, 1, 0, 0);
    const QVector3D eye = m_orbitTarget + rot.map(QVector3D(0, 0, m_orbitDistance));
    QMatrix4x4 view;
    view.lookAt(eye, m_orbitTarget, QVector3D(0, 1, 0));
    return view;
}

QMatrix4x4 PrevizViewport::currentProjection() const {
    const float widgetAspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 16.0f / 9.0f;
    if (usingCameraView()) return cameraProjection(m_frame, widgetAspect);
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 16.0f / 9.0f;
    QMatrix4x4 proj;
    proj.perspective(50.0f, aspect, 0.05f, 2000.0f);  // 作業視点は固定の見やすい画角
    return proj;
}

void PrevizViewport::drawPrimitive(const GpuPrimitive& prim, const QMatrix4x4& model, const QMatrix4x4& viewProj,
                                   bool unlit, bool highlight) {
    m_program->setUniformValue("uMvp", viewProj * model);
    m_program->setUniformValue("uModel", model);
    const QVector4D color = highlight ? (prim.color * 0.55f + kHighlightColor * 0.45f) : prim.color;
    m_program->setUniformValue("uColor", color);
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
        glDrawArrays(GL_LINES, 0, prim.lineVertexCount);
    }
    prim.vbo->release();
}

void PrevizViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    if (!m_program) return;
    renderScene(currentProjection() * currentView());
}

void PrevizViewport::renderScene(const QMatrix4x4& viewProj) {
    m_program->bind();
    m_program->setUniformValue("uLightDir", QVector3D(0.4f, 1.0f, 0.6f));

    // 床グリッド
    drawPrimitive(m_grid, QMatrix4x4(), viewProj, /*unlit=*/true);

    // モデル群(未配置なら目安キューブ)
    bool drewAny = false;
    if (m_scene) {
        for (size_t mi = 0; mi < m_scene->models.size(); ++mi) {
            const core::PrevizModel& model = m_scene->models[mi];
            GpuMesh* mesh = getOrLoadMesh(model.filePath);
            if (!mesh || mesh->loadFailed) continue;

            const core::PrevizTransform tf = model.transformAt(m_frame);
            QMatrix4x4 m;
            m.translate(tf.position.x, tf.position.y, tf.position.z);
            m.rotate(tf.rotationDeg.y, 0, 1, 0);
            m.rotate(tf.rotationDeg.x, 1, 0, 0);
            m.rotate(tf.rotationDeg.z, 0, 0, 1);
            m.scale(tf.scale.x, tf.scale.y, tf.scale.z);

            const bool highlight = !usingCameraView() && static_cast<int>(mi) == m_selectedModel;
            for (const GpuPrimitive& prim : mesh->primitives) {
                drawPrimitive(prim, m, viewProj, /*unlit=*/false, highlight);
                drewAny = true;
            }
        }
    }
    if (!drewAny) {
        drawPrimitive(m_placeholder, QMatrix4x4(), viewProj, /*unlit=*/false,
                      !usingCameraView() && m_selectedModel >= 0);
    }

    // 作業視点では本番カメラをギズモで示す
    if (!usingCameraView() && m_scene) {
        const core::PrevizCameraState camState = m_scene->camera.stateAt(m_frame);
        drawPrimitive(m_cameraGizmo, cameraWorldMatrix(camState), viewProj, /*unlit=*/true);
    }

    m_program->release();
}

// ナビ操作の書き込み先: キーが無ければ基本状態、キーがあれば現在コマのキー
// (=コマを選んでカメラを動かすとそのコマのカメラワークになる)
core::PrevizCameraState& PrevizViewport::editableCameraState() {
    auto& camera = m_scene->camera;
    if (camera.keys.empty()) return camera.state;
    camera.keys[m_frame] = camera.stateAt(m_frame);  // 補間値を起点にキー化
    return camera.keys[m_frame];
}

// モデルも同じ規則: キーが無ければ基本配置、あれば現在コマのキーを編集
core::PrevizTransform PrevizViewport::editableModelTransform(core::PrevizModel& model) const {
    return model.transformAt(m_frame);
}

void PrevizViewport::writeModelTransform(core::PrevizModel& model, const core::PrevizTransform& tf) const {
    if (model.transformKeys.empty()) {
        model.transform = tf;
    } else {
        model.transformKeys[m_frame] = tf;
    }
}

bool PrevizViewport::groundHit(QPointF mousePos, float planeY, QVector3D& out) const {
    // マウス位置→ワールドレイ→水平面(y=planeY)との交点
    const float ndcX = 2.0f * static_cast<float>(mousePos.x()) / static_cast<float>(width()) - 1.0f;
    const float ndcY = 1.0f - 2.0f * static_cast<float>(mousePos.y()) / static_cast<float>(height());
    bool invertible = false;
    const QMatrix4x4 invVp = (currentProjection() * currentView()).inverted(&invertible);
    if (!invertible) return false;

    const QVector3D nearPoint = invVp.map(QVector3D(ndcX, ndcY, -1.0f));
    const QVector3D farPoint = invVp.map(QVector3D(ndcX, ndcY, 1.0f));
    const QVector3D dir = farPoint - nearPoint;
    if (std::abs(dir.y()) < 1e-6f) return false;
    const float t = (planeY - nearPoint.y()) / dir.y();
    if (t < 0.0f) return false;
    out = nearPoint + dir * t;
    return true;
}

void PrevizViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::RightButton) m_looking = true;
    if (event->button() == Qt::MiddleButton) m_panningView = true;

    // 作業視点の左ドラッグ: 選択モデルの移動を開始
    if (!usingCameraView() && event->button() == Qt::LeftButton && m_scene && m_selectedModel >= 0 &&
        m_selectedModel < static_cast<int>(m_scene->models.size())) {
        core::PrevizModel& model = m_scene->models[static_cast<size_t>(m_selectedModel)];
        m_dragPlaneY = editableModelTransform(model).position.y;
        if (groundHit(event->position(), m_dragPlaneY, m_dragLastHit)) {
            m_draggingModel = true;
        }
    }
}

void PrevizViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!m_scene) return;
    const QPointF delta = event->position() - m_lastMousePos;
    const QPointF pos = event->position();
    m_lastMousePos = pos;

    // モデルのドラッグ移動(作業視点)
    if (m_draggingModel && m_selectedModel >= 0 && m_selectedModel < static_cast<int>(m_scene->models.size())) {
        core::PrevizModel& model = m_scene->models[static_cast<size_t>(m_selectedModel)];
        core::PrevizTransform tf = editableModelTransform(model);
        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+ドラッグ: 上下移動
            tf.position.y += static_cast<float>(-delta.y()) * 0.003f * m_orbitDistance;
            m_dragPlaneY = tf.position.y;
        } else {
            QVector3D hit;
            if (groundHit(pos, m_dragPlaneY, hit)) {
                tf.position.x += hit.x() - m_dragLastHit.x();
                tf.position.z += hit.z() - m_dragLastHit.z();
                m_dragLastHit = hit;
            }
        }
        writeModelTransform(model, tf);
        emit modelEdited();
        update();
        return;
    }

    if (!m_looking && !m_panningView) return;

    if (usingCameraView()) {
        // カメラ視点: 本番カメラを編集
        core::PrevizCameraState& cam = editableCameraState();
        if (m_looking) {
            cam.rotationDeg.y -= static_cast<float>(delta.x()) * 0.3f;
            cam.rotationDeg.x -= static_cast<float>(delta.y()) * 0.3f;
            cam.rotationDeg.x = std::clamp(cam.rotationDeg.x, -89.0f, 89.0f);
        } else if (m_panningView) {
            QMatrix4x4 rot;
            rot.rotate(cam.rotationDeg.y, 0, 1, 0);
            rot.rotate(cam.rotationDeg.x, 1, 0, 0);
            const QVector3D right = rot.map(QVector3D(1, 0, 0));
            const QVector3D up = rot.map(QVector3D(0, 1, 0));
            const float scale = 0.01f;
            const QVector3D move =
                right * static_cast<float>(-delta.x()) * scale + up * static_cast<float>(delta.y()) * scale;
            cam.position.x += move.x();
            cam.position.y += move.y();
            cam.position.z += move.z();
        }
        emit cameraEdited();
    } else {
        // 作業視点: オービットカメラ(シーンデータは変更しない)
        if (m_looking) {
            m_orbitYaw -= static_cast<float>(delta.x()) * 0.4f;
            m_orbitPitch -= static_cast<float>(delta.y()) * 0.4f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        } else if (m_panningView) {
            QMatrix4x4 rot;
            rot.rotate(m_orbitYaw, 0, 1, 0);
            rot.rotate(m_orbitPitch, 1, 0, 0);
            const QVector3D right = rot.map(QVector3D(1, 0, 0));
            const QVector3D up = rot.map(QVector3D(0, 1, 0));
            const float scale = 0.0015f * m_orbitDistance;
            m_orbitTarget += right * static_cast<float>(-delta.x()) * scale + up * static_cast<float>(delta.y()) * scale;
        }
    }
    update();
}

void PrevizViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) m_looking = false;
    if (event->button() == Qt::MiddleButton) m_panningView = false;
    if (event->button() == Qt::LeftButton) m_draggingModel = false;
}

void PrevizViewport::wheelEvent(QWheelEvent* event) {
    if (!m_scene) return;
    const int delta = event->angleDelta().y();
    if (usingCameraView()) {
        // Ctrl+ホイール: ズーム(焦点距離を変える)。ホイール単体のドリー(移動)とは別物
        if (event->modifiers() & Qt::ControlModifier) {
            core::PrevizCameraState& cam = editableCameraState();
            cam.focalLengthMm = std::clamp(cam.focalLengthMm + (delta > 0 ? 5.0f : -5.0f), 8.0f, 300.0f);
            emit cameraEdited();
            update();
            event->accept();
            return;
        }
        // カメラ視点: 前後ドリー(本番カメラを編集)
        core::PrevizCameraState& cam = editableCameraState();
        QMatrix4x4 rot;
        rot.rotate(cam.rotationDeg.y, 0, 1, 0);
        rot.rotate(cam.rotationDeg.x, 1, 0, 0);
        const QVector3D forward = rot.map(QVector3D(0, 0, -1));
        const float amount = delta > 0 ? 0.5f : -0.5f;
        cam.position.x += forward.x() * amount;
        cam.position.y += forward.y() * amount;
        cam.position.z += forward.z() * amount;
        emit cameraEdited();
    } else {
        // 作業視点: 注視点への距離を変える
        m_orbitDistance *= (delta > 0) ? 0.88f : 1.14f;
        m_orbitDistance = std::clamp(m_orbitDistance, 0.3f, 300.0f);
    }
    update();
    event->accept();
}

void PrevizViewport::moveFreely(const QVector3D& localDir, float step) {
    if (!m_scene) return;
    if (usingCameraView()) {
        // カメラ視点: 本番カメラをカメラ向き基準で移動(キー規則適用)
        core::PrevizCameraState& cam = editableCameraState();
        QMatrix4x4 rot;
        rot.rotate(cam.rotationDeg.y, 0, 1, 0);
        rot.rotate(cam.rotationDeg.x, 1, 0, 0);
        const QVector3D move = rot.map(QVector3D(localDir.x(), localDir.y(), -localDir.z())) * step;
        cam.position.x += move.x();
        cam.position.y += move.y();
        cam.position.z += move.z();
        emit cameraEdited();
    } else {
        // 作業視点: 注視点を視点向き基準で移動(飛行)
        QMatrix4x4 rot;
        rot.rotate(m_orbitYaw, 0, 1, 0);
        rot.rotate(m_orbitPitch, 1, 0, 0);
        m_orbitTarget += rot.map(QVector3D(localDir.x(), localDir.y(), -localDir.z())) * step;
    }
    update();
}

void PrevizViewport::keyPressEvent(QKeyEvent* event) {
    // FPSゲーム式の移動: WASD=前後左右、Q/E=下/上。Shift併用で3倍速。
    // キーリピートで押しっぱなし移動になる
    const float step = (event->modifiers() & Qt::ShiftModifier) ? 0.6f : 0.2f;
    switch (event->key()) {
        case Qt::Key_W:
            moveFreely({0, 0, 1}, step);
            break;
        case Qt::Key_S:
            moveFreely({0, 0, -1}, step);
            break;
        case Qt::Key_A:
            moveFreely({-1, 0, 0}, step);
            break;
        case Qt::Key_D:
            moveFreely({1, 0, 0}, step);
            break;
        case Qt::Key_Q:
            moveFreely({0, -1, 0}, step);
            break;
        case Qt::Key_E:
            moveFreely({0, 1, 0}, step);
            break;
        default:
            QOpenGLWidget::keyPressEvent(event);
            return;
    }
    event->accept();
}
