#include "PrevizViewport.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QDebug>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

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

bool isHumanoidModelPath(const std::string& filePath) {
    return filePath == ":humanoid";
}

QMatrix4x4 modelMatrixFromTransform(const core::PrevizTransform& tf) {
    QMatrix4x4 m;
    m.translate(tf.position.x, tf.position.y, tf.position.z);
    m.rotate(tf.rotationDeg.y, 0, 1, 0);
    m.rotate(tf.rotationDeg.x, 1, 0, 0);
    m.rotate(tf.rotationDeg.z, 0, 0, 1);
    m.scale(tf.scale.x, tf.scale.y, tf.scale.z);
    return m;
}

std::vector<uint32_t> triangleWireIndices(const uint32_t* indices, size_t count) {
    std::vector<uint32_t> wire;
    wire.reserve(count * 2);
    for (size_t i = 0; i + 2 < count; i += 3) {
        const uint32_t a = indices[i];
        const uint32_t b = indices[i + 1];
        const uint32_t c = indices[i + 2];
        wire.insert(wire.end(), {a, b, b, c, c, a});
    }
    return wire;
}

std::vector<uint32_t> triangleWireIndices(const std::vector<uint32_t>& indices) {
    return triangleWireIndices(indices.data(), indices.size());
}

const char* kCompatVertexShader = R"(#version 120
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

const char* kCompatFragmentShader = R"(#version 120
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform float uUnlit;

varying vec3 vNormal;

void main() {
    if (uUnlit > 0.5) {
        gl_FragColor = uColor;
        return;
    }

    float d = max(
        dot(normalize(vNormal), normalize(uLightDir)),
        0.0
    );

    gl_FragColor = vec4(
        uColor.rgb * (0.35 + 0.65 * d),
        uColor.a
    );
}
)";

const char* kCoreVertexShader = R"(#version 150 core
in vec3 aPos;
in vec3 aNormal;

uniform mat4 uMvp;
uniform mat4 uModel;

out vec3 vNormal;

void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
}
)";

const char* kCoreFragmentShader = R"(#version 150 core
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform float uUnlit;

in vec3 vNormal;
out vec4 fragColor;

void main() {
    if (uUnlit > 0.5) {
        fragColor = uColor;
        return;
    }

    float d = max(
        dot(normalize(vNormal), normalize(uLightDir)),
        0.0
    );

    fragColor = vec4(
        uColor.rgb * (0.35 + 0.65 * d),
        uColor.a
    );
}
)";

const char* kEsVertexShader = R"(#version 100
attribute highp vec3 aPos;
attribute highp vec3 aNormal;

uniform highp mat4 uMvp;
uniform highp mat4 uModel;

varying highp vec3 vNormal;

void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
}
)";

const char* kEsFragmentShader = R"(#version 100
precision highp float;

uniform vec4 uColor;
uniform vec3 uLightDir;
uniform float uUnlit;

varying vec3 vNormal;

void main() {
    if (uUnlit > 0.5) {
        gl_FragColor = uColor;
        return;
    }

    float d = max(
        dot(normalize(vNormal), normalize(uLightDir)),
        0.0
    );

    gl_FragColor = vec4(
        uColor.rgb * (0.35 + 0.65 * d),
        uColor.a
    );
}
)";


// レンズ歪曲ポスト処理: 全画面クアッド(pos.xy + uv)をそのまま出す頂点シェーダ
const char* kPostCompatVertexShader = R"(#version 120
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// 放射状のレンズ歪曲。uDistort>0=樽型/魚眼(中心が拡大・周辺が圧縮)、<0=糸巻き型。
// アスペクト補正して円形の歪みにする。標準レンズ相当の直線→直線は uDistort=0
const char* kPostCompatFragmentShader = R"(#version 120
uniform sampler2D uTex;
uniform float uDistort;
uniform float uAspect;
varying vec2 vUv;
void main() {
    vec2 d = vUv - 0.5;
    vec2 dc = vec2(d.x * uAspect, d.y);
    float r2 = dot(dc, dc);
    float f = 1.0 - uDistort * r2;
    vec2 uv = 0.5 + d * f;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.16, 0.17, 0.20, 1.0);
        return;
    }
    gl_FragColor = texture2D(uTex, uv);
}
)";

const char* kPostCoreVertexShader = R"(#version 150 core
in vec2 aPos;
in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kPostCoreFragmentShader = R"(#version 150 core
uniform sampler2D uTex;
uniform float uDistort;
uniform float uAspect;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec2 d = vUv - 0.5;
    vec2 dc = vec2(d.x * uAspect, d.y);
    float r2 = dot(dc, dc);
    float f = 1.0 - uDistort * r2;
    vec2 uv = 0.5 + d * f;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.16, 0.17, 0.20, 1.0);
        return;
    }
    fragColor = texture(uTex, uv);
}
)";

const char* kPostEsVertexShader = R"(#version 100
attribute highp vec2 aPos;
attribute highp vec2 aUv;
varying highp vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kPostEsFragmentShader = R"(#version 100
precision highp float;
uniform sampler2D uTex;
uniform float uDistort;
uniform float uAspect;
varying highp vec2 vUv;
void main() {
    vec2 d = vUv - 0.5;
    vec2 dc = vec2(d.x * uAspect, d.y);
    float r2 = dot(dc, dc);
    float f = 1.0 - uDistort * r2;
    vec2 uv = 0.5 + d * f;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.16, 0.17, 0.20, 1.0);
        return;
    }
    gl_FragColor = texture2D(uTex, uv);
}
)";

enum class ShaderFlavor {
    Compatibility,
    Core,
    Es,
};

ShaderFlavor currentShaderFlavor() {
    const QOpenGLContext* glContext = QOpenGLContext::currentContext();
    if (glContext && glContext->isOpenGLES()) return ShaderFlavor::Es;
    if (glContext && glContext->format().profile() == QSurfaceFormat::CoreProfile) {
        return ShaderFlavor::Core;
    }
    return ShaderFlavor::Compatibility;
}

const char* meshVertexShader(ShaderFlavor flavor) {
    switch (flavor) {
        case ShaderFlavor::Es:
            return kEsVertexShader;
        case ShaderFlavor::Core:
            return kCoreVertexShader;
        case ShaderFlavor::Compatibility:
        default:
            return kCompatVertexShader;
    }
}

const char* meshFragmentShader(ShaderFlavor flavor) {
    switch (flavor) {
        case ShaderFlavor::Es:
            return kEsFragmentShader;
        case ShaderFlavor::Core:
            return kCoreFragmentShader;
        case ShaderFlavor::Compatibility:
        default:
            return kCompatFragmentShader;
    }
}

const char* postVertexShader(ShaderFlavor flavor) {
    switch (flavor) {
        case ShaderFlavor::Es:
            return kPostEsVertexShader;
        case ShaderFlavor::Core:
            return kPostCoreVertexShader;
        case ShaderFlavor::Compatibility:
        default:
            return kPostCompatVertexShader;
    }
}

const char* postFragmentShader(ShaderFlavor flavor) {
    switch (flavor) {
        case ShaderFlavor::Es:
            return kPostEsFragmentShader;
        case ShaderFlavor::Core:
            return kPostCoreFragmentShader;
        case ShaderFlavor::Compatibility:
        default:
            return kPostCompatFragmentShader;
    }
}

const QVector4D kGizmoColor(1.0f, 0.6f, 0.2f, 1.0f);      // 本番カメラのギズモ(オレンジ)
const QVector4D kHighlightColor(1.0f, 0.65f, 0.25f, 1.0f);  // 選択モデルの強調色

}  // namespace

PrevizViewport::PrevizViewport(QWidget* parent)
    : QOpenGLWidget(parent) {
    /*
     * Windows?Core Profile?OpenGL ES?????????
     * ??GLSL?????????????????
     *
     * ??????????OpenGL 2.1??????????????
     */
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(2, 1);
    format.setProfile(QSurfaceFormat::NoProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);

    setMinimumSize(480, 270);
    setFocusPolicy(Qt::ClickFocus);
}

PrevizViewport::~PrevizViewport() {
    makeCurrent();
    m_meshCache.clear();
    m_grid = {};
    m_placeholder = {};
    m_cameraGizmo = {};
    m_program.reset();
    m_postProgram.reset();
    m_postQuad.reset();
    m_sceneFbo.reset();
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

void PrevizViewport::setWireframeEnabled(bool enabled) {
    if (m_wireframeEnabled == enabled) return;
    m_wireframeEnabled = enabled;
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
    // 本番カメラの絵なのでレンズ歪曲(魚眼/樽/糸巻き)を反映する。この関数は常にカメラビュー扱い
    renderSceneWithLens(viewProj, kWidth, kHeight, m_scene ? m_scene->camera.lensDistortion : 0.0f);
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
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    const QOpenGLContext* glContext =
        QOpenGLContext::currentContext();
    const ShaderFlavor shaderFlavor = currentShaderFlavor();
    const char* vertexSource = meshVertexShader(shaderFlavor);
    const char* fragmentSource = meshFragmentShader(shaderFlavor);

    m_program = std::make_unique<QOpenGLShaderProgram>();

    if (!m_program->addShaderFromSourceCode(
            QOpenGLShader::Vertex,
            vertexSource)) {
        qCritical().noquote()
            << "Previz vertex shader compile failed:"
            << m_program->log();

        m_program.reset();
        return;
    }

    if (!m_program->addShaderFromSourceCode(
            QOpenGLShader::Fragment,
            fragmentSource)) {
        qCritical().noquote()
            << "Previz fragment shader compile failed:"
            << m_program->log();

        m_program.reset();
        return;
    }

    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aNormal", 1);

    if (!m_program->link()) {
        qCritical().noquote()
            << "Previz shader link failed:"
            << m_program->log();

        m_program.reset();
        return;
    }

    if (!m_program->bind()) {
        qCritical().noquote()
            << "Previz shader bind failed:"
            << m_program->log();

        m_program.reset();
        return;
    }

    m_program->release();

    if (glContext) {
        const QSurfaceFormat actualFormat =
            glContext->format();

        qInfo()
            << "Previz OpenGL:"
            << actualFormat.majorVersion()
            << "."
            << actualFormat.minorVersion()
            << "profile="
            << actualFormat.profile()
            << "GLES="
            << glContext->isOpenGLES();
    }

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
    const std::vector<uint32_t> wireIdx = triangleWireIndices(idx, sizeof(idx) / sizeof(idx[0]));
    m_placeholder.wireIbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
    m_placeholder.wireIbo->create();
    m_placeholder.wireIbo->bind();
    m_placeholder.wireIbo->allocate(wireIdx.data(), static_cast<int>(wireIdx.size() * sizeof(uint32_t)));
    m_placeholder.wireIbo->release();
    m_placeholder.wireIndexCount = static_cast<int>(wireIdx.size());
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
            const std::vector<uint32_t> wireIndices = triangleWireIndices(src.indices);
            prim.wireIbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
            prim.wireIbo->create();
            prim.wireIbo->bind();
            prim.wireIbo->allocate(wireIndices.data(), static_cast<int>(wireIndices.size() * sizeof(uint32_t)));
            prim.wireIbo->release();
            prim.wireIndexCount = static_cast<int>(wireIndices.size());
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

void PrevizViewport::drawPrimitive(
    const GpuPrimitive& prim,
    const QMatrix4x4& model,
    const QMatrix4x4& viewProj,
    bool unlit,
    bool highlight,
    const QVector4D* colorOverride) {
    if (!m_program ||
        !m_program->isLinked() ||
        !prim.vbo ||
        !prim.vbo->isCreated()) {
        return;
    }

    m_program->setUniformValue(
        "uMvp",
        viewProj * model
    );

    m_program->setUniformValue(
        "uModel",
        model
    );

    const QVector4D baseColor = colorOverride ? *colorOverride : prim.color;
    const QVector4D color =
        highlight
            ? (baseColor * 0.55f +
               kHighlightColor * 0.45f)
            : baseColor;

    m_program->setUniformValue(
        "uColor",
        color
    );

    m_program->setUniformValue(
        "uUnlit",
        (unlit || (m_wireframeEnabled && prim.wireIbo)) ? 1.0f : 0.0f
    );

    if (!prim.vbo->bind()) {
        return;
    }

    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);

    m_program->setAttributeBuffer(
        0,
        GL_FLOAT,
        0,
        3,
        6 * sizeof(float)
    );

    m_program->setAttributeBuffer(
        1,
        GL_FLOAT,
        3 * sizeof(float),
        3,
        6 * sizeof(float)
    );

    if (m_wireframeEnabled &&
        prim.wireIbo &&
        prim.wireIbo->isCreated() &&
        prim.wireIbo->bind()) {
        glLineWidth(1.35f);
        glDrawElements(
            GL_LINES,
            prim.wireIndexCount,
            GL_UNSIGNED_INT,
            nullptr
        );
        glLineWidth(1.0f);
        prim.wireIbo->release();
    } else if (prim.ibo &&
        prim.ibo->isCreated() &&
        prim.ibo->bind()) {
        glDrawElements(
            GL_TRIANGLES,
            prim.indexCount,
            GL_UNSIGNED_INT,
            nullptr
        );

        prim.ibo->release();
    } else {
        glDrawArrays(
            GL_LINES,
            0,
            prim.lineVertexCount
        );
    }

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);

    prim.vbo->release();
}

bool PrevizViewport::drawMesh(
    const std::string& filePath,
    const QMatrix4x4& model,
    const QMatrix4x4& viewProj,
    bool unlit,
    bool highlight,
    const QVector4D* colorOverride) {
    GpuMesh* mesh = getOrLoadMesh(filePath);
    if (!mesh || mesh->loadFailed) return false;

    bool drew = false;
    for (const GpuPrimitive& prim : mesh->primitives) {
        drawPrimitive(prim, model, viewProj, unlit, highlight, colorOverride);
        drew = true;
    }
    return drew;
}

void PrevizViewport::drawHumanoid(
    const core::PrevizModel& model,
    const QMatrix4x4& modelMatrix,
    const QMatrix4x4& viewProj,
    bool highlight) {
    const core::PrevizHumanoidPose pose = model.poseAt(m_frame);
    const core::PrevizHumanoidBody body = model.humanoidBody;

    const QVector4D skinColor(0.78f, 0.62f, 0.48f, 1.0f);
    const QVector4D chestColor(0.38f, 0.50f, 0.74f, 1.0f);
    const QVector4D bellyColor(0.32f, 0.44f, 0.66f, 1.0f);
    const QVector4D waistColor(0.24f, 0.35f, 0.54f, 1.0f);
    const QVector4D limbColor(0.46f, 0.56f, 0.74f, 1.0f);
    const QVector4D jointColor(0.24f, 0.28f, 0.36f, 1.0f);
    const QVector4D footColor(0.18f, 0.18f, 0.20f, 1.0f);

    const auto ratio = [](float value, float minValue = 0.2f, float maxValue = 3.0f) {
        return std::clamp(value, minValue, maxValue);
    };
    const float headScale = ratio(body.headScale);
    const float torsoLength = ratio(body.torsoLength);
    const float chestWidth = ratio(body.chestWidth);
    const float bellyWidth = ratio(body.bellyWidth);
    const float waistWidth = ratio(body.waistWidth);
    const float shoulderWidth = ratio(body.shoulderWidth);
    const float hipWidth = ratio(body.hipWidth);
    const float armLengthScale = ratio(body.armLength);
    const float armThickness = ratio(body.armThickness);
    const float legLengthScale = ratio(body.legLength);
    const float legThickness = ratio(body.legThickness);
    const float handScale = ratio(body.handScale);
    const float footScale = ratio(body.footScale);
    const float leftArmLengthScale = armLengthScale * ratio(body.leftArmLength);
    const float rightArmLengthScale = armLengthScale * ratio(body.rightArmLength);
    const float leftArmThickness = armThickness * ratio(body.leftArmThickness);
    const float rightArmThickness = armThickness * ratio(body.rightArmThickness);
    const float leftLegLengthScale = legLengthScale * ratio(body.leftLegLength);
    const float rightLegLengthScale = legLengthScale * ratio(body.rightLegLength);
    const float leftLegThickness = legThickness * ratio(body.leftLegThickness);
    const float rightLegThickness = legThickness * ratio(body.rightLegThickness);
    const float leftHandScale = handScale * ratio(body.leftHandScale);
    const float rightHandScale = handScale * ratio(body.rightHandScale);
    const float leftFootScale = footScale * ratio(body.leftFootScale);
    const float rightFootScale = footScale * ratio(body.rightFootScale);

    const auto drawLocal = [&](const std::string& filePath, const QMatrix4x4& local, const QVector4D& color) {
        drawMesh(filePath, modelMatrix * local, viewProj, /*unlit=*/false, highlight, &color);
    };

    const auto sphereAt = [&](const QVector3D& center, float radius, const QVector4D& color) {
        QMatrix4x4 local;
        local.translate(center.x(), center.y() - radius, center.z());
        local.scale(radius * 2.0f, radius * 2.0f, radius * 2.0f);
        drawLocal(":sphere", local, color);
    };

    const auto ellipsoidAt = [&](const QVector3D& center, const QMatrix4x4& rot, float rx, float ry, float rz,
                                 const QVector4D& color) {
        QMatrix4x4 local;
        local.translate(center.x(), center.y(), center.z());
        local = local * rot;
        local.translate(0.0f, -ry, 0.0f);
        local.scale(rx * 2.0f, ry * 2.0f, rz * 2.0f);
        drawLocal(":sphere", local, color);
    };

    const auto segmentMatrix = [](const QVector3D& anchor, const QMatrix4x4& rot, float length, float radius) {
        QMatrix4x4 local;
        local.translate(anchor.x(), anchor.y(), anchor.z());
        local = local * rot;
        local.scale(radius * 2.0f, length, radius * 2.0f);
        return local;
    };

    const auto segmentEnd = [](const QVector3D& anchor, const QMatrix4x4& rot, float length) {
        return anchor + rot.mapVector(QVector3D(0.0f, length, 0.0f));
    };

    const auto downwardRot = [](float pitchDeg, float rollDeg) {
        QMatrix4x4 rot;
        rot.rotate(rollDeg, 0, 0, 1);
        rot.rotate(pitchDeg, 1, 0, 0);
        rot.rotate(180.0f, 1, 0, 0);
        return rot;
    };

    QMatrix4x4 torsoRot;
    torsoRot.rotate(pose.torsoRollDeg, 0, 0, 1);
    torsoRot.rotate(pose.torsoPitchDeg, 1, 0, 0);

    const float leftUpperLeg = 0.62f * leftLegLengthScale;
    const float leftLowerLeg = 0.58f * leftLegLengthScale;
    const float rightUpperLeg = 0.62f * rightLegLengthScale;
    const float rightLowerLeg = 0.58f * rightLegLengthScale;
    const float pelvisY = std::max(0.35f, std::max(leftUpperLeg + leftLowerLeg, rightUpperLeg + rightLowerLeg) - 0.18f);

    const QVector3D pelvis(0.0f, pelvisY, 0.0f);
    const auto torsoPoint = [&](const QVector3D& p) {
        return pelvis + torsoRot.mapVector(p);
    };

    const float waistHeight = 0.22f * torsoLength;
    const float bellyHeight = 0.30f * torsoLength;
    const float chestHeight = 0.34f * torsoLength;
    const float torsoHeight = waistHeight + bellyHeight + chestHeight;
    const float torsoGap = 0.025f;
    const auto drawTorsoPart = [&](float offset, float height, float width, const QVector4D& color) {
        const float visibleHeight = std::max(0.06f, height - torsoGap);
        const float centerOffset = offset + height * 0.5f;
        const float rz = 0.16f * (0.75f + width * 0.25f);
        ellipsoidAt(torsoPoint(QVector3D(0.0f, centerOffset, 0.0f)), torsoRot, 0.30f * width,
                    visibleHeight * 0.52f, rz, color);
    };

    drawTorsoPart(0.0f, waistHeight, waistWidth, waistColor);
    drawTorsoPart(waistHeight, bellyHeight, bellyWidth, bellyColor);
    drawTorsoPart(waistHeight + bellyHeight, chestHeight, chestWidth, chestColor);

    const float shoulderHalf = 0.43f * shoulderWidth * std::max(0.75f, chestWidth);
    const float hipHalf = 0.23f * hipWidth * std::max(0.75f, waistWidth);
    const QVector3D chestTop = torsoPoint(QVector3D(0.0f, torsoHeight + 0.02f, 0.0f));
    const QVector3D leftShoulder = torsoPoint(QVector3D(-shoulderHalf, waistHeight + bellyHeight + chestHeight * 0.70f, 0.0f));
    const QVector3D rightShoulder = torsoPoint(QVector3D(shoulderHalf, waistHeight + bellyHeight + chestHeight * 0.70f, 0.0f));
    const QVector3D leftHip = torsoPoint(QVector3D(-hipHalf, 0.03f, 0.0f));
    const QVector3D rightHip = torsoPoint(QVector3D(hipHalf, 0.03f, 0.0f));

    sphereAt(chestTop, 0.095f * std::max(0.85f, chestWidth), jointColor);
    sphereAt(leftShoulder, 0.105f * leftArmThickness, jointColor);
    sphereAt(rightShoulder, 0.105f * rightArmThickness, jointColor);
    sphereAt(leftHip, 0.105f * leftLegThickness, jointColor);
    sphereAt(rightHip, 0.105f * rightLegThickness, jointColor);

    QMatrix4x4 headRot = torsoRot;
    headRot.rotate(pose.headYawDeg, 0, 1, 0);
    headRot.rotate(pose.headPitchDeg, 1, 0, 0);
    const float headRadius = 0.22f * headScale;
    const QVector3D headCenter = torsoPoint(QVector3D(0.0f, torsoHeight + headRadius * 1.08f, 0.0f));
    sphereAt(headCenter, headRadius, skinColor);
    QMatrix4x4 face;
    face.translate(headCenter.x(), headCenter.y(), headCenter.z());
    face = face * headRot;
    face.translate(0.0f, -headRadius * 0.25f, -headRadius * 0.90f);
    face.scale(0.12f * headScale, 0.08f * headScale, 0.08f * headScale);
    drawLocal(":box", face, skinColor);

    auto drawArm = [&](const QVector3D& shoulder, float shoulderPitch, float shoulderRoll, float elbowDeg,
                       float lengthScale, float thicknessScale, float handScaleValue) {
        const float upperArm = 0.52f * lengthScale;
        const float forearm = 0.48f * lengthScale;
        QMatrix4x4 upperRot = torsoRot * downwardRot(shoulderPitch, shoulderRoll);
        drawLocal(":cylinder", segmentMatrix(shoulder, upperRot, upperArm, 0.075f * thicknessScale), limbColor);
        const QVector3D elbow = segmentEnd(shoulder, upperRot, upperArm);
        sphereAt(elbow, 0.085f * thicknessScale, jointColor);
        QMatrix4x4 lowerRot = upperRot;
        lowerRot.rotate(elbowDeg, 1, 0, 0);
        drawLocal(":cylinder", segmentMatrix(elbow, lowerRot, forearm, 0.065f * thicknessScale), limbColor);
        sphereAt(segmentEnd(elbow, lowerRot, forearm), 0.075f * handScaleValue * thicknessScale, skinColor);
    };

    auto drawLeg = [&](const QVector3D& hip, float hipPitch, float hipRoll, float kneeDeg, float upperLeg,
                       float lowerLeg, float thicknessScale, float footScaleValue) {
        QMatrix4x4 upperRot = torsoRot * downwardRot(hipPitch, hipRoll);
        drawLocal(":cylinder", segmentMatrix(hip, upperRot, upperLeg, 0.095f * thicknessScale), limbColor);
        const QVector3D knee = segmentEnd(hip, upperRot, upperLeg);
        sphereAt(knee, 0.095f * thicknessScale, jointColor);
        QMatrix4x4 lowerRot = upperRot;
        lowerRot.rotate(kneeDeg, 1, 0, 0);
        drawLocal(":cylinder", segmentMatrix(knee, lowerRot, lowerLeg, 0.08f * thicknessScale), limbColor);
        const QVector3D ankle = segmentEnd(knee, lowerRot, lowerLeg);
        QMatrix4x4 foot;
        foot.translate(ankle.x(), ankle.y() - 0.05f * footScaleValue, ankle.z() - 0.12f * footScaleValue);
        foot.scale(0.22f * footScaleValue, 0.08f * thicknessScale, 0.38f * footScaleValue);
        drawLocal(":box", foot, footColor);
    };

    drawArm(leftShoulder, pose.leftShoulderPitchDeg, pose.leftShoulderRollDeg, pose.leftElbowDeg,
            leftArmLengthScale, leftArmThickness, leftHandScale);
    drawArm(rightShoulder, pose.rightShoulderPitchDeg, pose.rightShoulderRollDeg, pose.rightElbowDeg,
            rightArmLengthScale, rightArmThickness, rightHandScale);
    drawLeg(leftHip, pose.leftHipPitchDeg, pose.leftHipRollDeg, pose.leftKneeDeg, leftUpperLeg, leftLowerLeg,
            leftLegThickness, leftFootScale);
    drawLeg(rightHip, pose.rightHipPitchDeg, pose.rightHipRollDeg, pose.rightKneeDeg, rightUpperLeg, rightLowerLeg,
            rightLegThickness, rightFootScale);
}

float PrevizViewport::currentLensDistortion() const {
    // レンズ歪曲は本番カメラの絵にのみ効かせる(作業オービット視点は歪ませない)
    if (!usingCameraView() || !m_scene) return 0.0f;
    return m_scene->camera.lensDistortion;
}

void PrevizViewport::ensurePostResources() {
    if (!m_postProgram) {
        const ShaderFlavor shaderFlavor = currentShaderFlavor();
        m_postProgram = std::make_unique<QOpenGLShaderProgram>();

        if (!m_postProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, postVertexShader(shaderFlavor))) {
            qCritical().noquote()
                << "Previz post vertex shader compile failed:"
                << m_postProgram->log();
            m_postProgram.reset();
            return;
        }

        if (!m_postProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, postFragmentShader(shaderFlavor))) {
            qCritical().noquote()
                << "Previz post fragment shader compile failed:"
                << m_postProgram->log();
            m_postProgram.reset();
            return;
        }

        m_postProgram->bindAttributeLocation("aPos", 0);
        m_postProgram->bindAttributeLocation("aUv", 1);

        if (!m_postProgram->link()) {
            qCritical().noquote()
                << "Previz post shader link failed:"
                << m_postProgram->log();
            m_postProgram.reset();
            return;
        }
    }
    if (!m_postQuad) {
        // 2三角形の全画面クアッド: 各頂点 pos.xy, uv.xy
        const float verts[] = {
            -1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f,
            -1.f, 1.f,  0.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f,  1.f, 1.f, 1.f,
        };
        m_postQuad = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        m_postQuad->create();
        m_postQuad->bind();
        m_postQuad->allocate(verts, sizeof(verts));
        m_postQuad->release();
    }
}

void PrevizViewport::renderSceneWithLens(const QMatrix4x4& viewProj, int w, int h, float distortion) {
    if (std::abs(distortion) < 1e-4f || w <= 0 || h <= 0) {
        renderScene(viewProj);  // 歪みなし: 現在の描画先へ直接
        return;
    }
    ensurePostResources();
    if (!m_postProgram || !m_postProgram->isLinked() || !m_postQuad || !m_postQuad->isCreated()) {
        renderScene(viewProj);
        return;
    }

    // 現在の描画先FBO/ビューポートを退避(paintGLは既定FBO、renderCameraViewImageは外側FBO)
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevVp[4];
    glGetIntegerv(GL_VIEWPORT, prevVp);

    // シーンをいったんオフスクリーンFBOへ描く(サイズが変わったら作り直す)
    if (!m_sceneFbo || m_sceneFbo->width() != w || m_sceneFbo->height() != h) {
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        m_sceneFbo = std::make_unique<QOpenGLFramebufferObject>(w, h, f);
    }
    m_sceneFbo->bind();
    glViewport(0, 0, w, h);
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    renderScene(viewProj);

    // 描画先を元に戻し、歪曲シェーダで全画面クアッドを合成する
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    glDisable(GL_DEPTH_TEST);

    m_postProgram->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneFbo->texture());
    m_postProgram->setUniformValue("uTex", 0);
    m_postProgram->setUniformValue("uDistort", distortion);
    m_postProgram->setUniformValue("uAspect", static_cast<float>(w) / static_cast<float>(h));

    m_postQuad->bind();
    m_postProgram->enableAttributeArray(0);
    m_postProgram->enableAttributeArray(1);
    m_postProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_postProgram->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_postQuad->release();
    m_postProgram->release();

    glEnable(GL_DEPTH_TEST);
    m_program->bind();  // 呼び出し側(renderScene後の状態)に合わせて主シェーダへ戻す
}

void PrevizViewport::paintGL() {
    const int viewportWidth = std::max(
        1,
        static_cast<int>(
            std::lround(
                static_cast<double>(width()) *
                devicePixelRatioF()
            )
        )
    );

    const int viewportHeight = std::max(
        1,
        static_cast<int>(
            std::lround(
                static_cast<double>(height()) *
                devicePixelRatioF()
            )
        )
    );

    glViewport(
        0,
        0,
        viewportWidth,
        viewportHeight
    );

    glColorMask(
        GL_TRUE,
        GL_TRUE,
        GL_TRUE,
        GL_TRUE
    );

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glClearColor(
        0.16f,
        0.17f,
        0.20f,
        1.0f
    );

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    if (!m_program ||
        !m_program->isLinked()) {
        return;
    }

    // QOpenGLWidgetの実ピクセルサイズ(devicePixelRatio込み)は現在のGLビューポートから取得する
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    renderSceneWithLens(currentProjection() * currentView(), vp[2], vp[3], currentLensDistortion());
}

void PrevizViewport::renderScene(const QMatrix4x4& viewProj) {
    if (!m_program ||
        !m_program->isLinked() ||
        !m_program->bind()) {
        return;
    }
    m_program->setUniformValue("uLightDir", QVector3D(0.4f, 1.0f, 0.6f));

    // 床グリッド
    drawPrimitive(m_grid, QMatrix4x4(), viewProj, /*unlit=*/true);

    // モデル群(未配置なら目安キューブ)
    bool drewAny = false;
    if (m_scene) {
        for (size_t mi = 0; mi < m_scene->models.size(); ++mi) {
            const core::PrevizModel& model = m_scene->models[mi];
            const core::PrevizTransform tf = model.transformAt(m_frame);
            const QMatrix4x4 m = modelMatrixFromTransform(tf);
            const bool highlight = !usingCameraView() && static_cast<int>(mi) == m_selectedModel;

            if (isHumanoidModelPath(model.filePath)) {
                drawHumanoid(model, m, viewProj, highlight);
                drewAny = true;
                continue;
            }

            if (drawMesh(model.filePath, m, viewProj, /*unlit=*/false, highlight)) drewAny = true;
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
