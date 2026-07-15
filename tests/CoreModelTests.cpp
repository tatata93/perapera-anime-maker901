#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "core/Compositor.h"
#include "core/Project.h"
#include "core/ProjectIO.h"

TEST_CASE("Project owns Scene -> Cut -> Cel -> Layer -> Frame hierarchy", "[core]") {
    core::Project project("Test Project");

    core::Scene& scene = project.addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Cel& cel = cut.addCel("Cel A");
    core::Layer& layer = cel.addLayer("Layer 1");
    core::Frame& frame = layer.addFrame();

    SECTION("hierarchy is reachable via index accessors") {
        REQUIRE(project.sceneCount() == 1);
        REQUIRE(project.scene(0).name() == "Scene 1");
        REQUIRE(project.scene(0).cutCount() == 1);
        REQUIRE(project.scene(0).cut(0).name() == "Cut 1");
        REQUIRE(project.scene(0).cut(0).celCount() == 1);
        REQUIRE(project.scene(0).cut(0).cel(0).name() == "Cel A");
        REQUIRE(project.scene(0).cut(0).cel(0).layerCount() == 1);
        REQUIRE(project.scene(0).cut(0).cel(0).layer(0).name() == "Layer 1");
        REQUIRE(project.scene(0).cut(0).cel(0).layer(0).frameCount() == 1);
    }

    SECTION("frame bitmap can be resized and edited") {
        frame.bitmap() = core::Bitmap(4, 4);
        frame.bitmap().setPixel(1, 1, {255, 0, 0, 255});

        REQUIRE(frame.bitmap().width() == 4);
        REQUIRE(frame.bitmap().height() == 4);

        auto px = frame.bitmap().pixel(1, 1);
        REQUIRE(px.r == 255);
        REQUIRE(px.a == 255);
    }

    SECTION("removing a frame updates frame count") {
        layer.addFrame();
        REQUIRE(layer.frameCount() == 2);
        layer.removeFrame(0);
        REQUIRE(layer.frameCount() == 1);
    }

    SECTION("inserting a frame at an index places it there") {
        layer.frame(0).bitmap() = core::Bitmap(2, 2);  // 既存フレームに目印
        layer.addFrame();                              // [marked, blank]
        core::Frame& inserted = layer.insertFrame(1);  // [marked, inserted, blank]
        inserted.bitmap() = core::Bitmap(8, 8);

        REQUIRE(layer.frameCount() == 3);
        REQUIRE(layer.frame(0).bitmap().width() == 2);
        REQUIRE(layer.frame(1).bitmap().width() == 8);
        REQUIRE(layer.frame(2).bitmap().isEmpty());
    }
}

TEST_CASE("Layer and Cel visibility default to true and can be toggled", "[core]") {
    core::Layer layer("Layer 1");
    REQUIRE(layer.visible());
    layer.setVisible(false);
    REQUIRE_FALSE(layer.visible());

    core::Cel cel("Cel A");
    REQUIRE(cel.visible());
    cel.setVisible(false);
    REQUIRE_FALSE(cel.visible());
}

TEST_CASE("Layer role defaults to Normal and can be changed", "[core]") {
    core::Layer layer("Layer 1");
    REQUIRE(layer.role() == core::LayerRole::Normal);

    layer.setRole(core::LayerRole::ColorTrace);
    REQUIRE(layer.role() == core::LayerRole::ColorTrace);

    layer.setRole(core::LayerRole::Correction);
    REQUIRE(layer.role() == core::LayerRole::Correction);
}

TEST_CASE("Cel::moveLayer reorders layers", "[core]") {
    core::Cel cel("Cel A");
    cel.addLayer("Layer 1");
    cel.addLayer("Layer 2");
    cel.addLayer("Layer 3");

    SECTION("moving forward shifts intervening layers back") {
        cel.moveLayer(0, 2);
        REQUIRE(cel.layer(0).name() == "Layer 2");
        REQUIRE(cel.layer(1).name() == "Layer 3");
        REQUIRE(cel.layer(2).name() == "Layer 1");
    }

    SECTION("moving backward shifts intervening layers forward") {
        cel.moveLayer(2, 0);
        REQUIRE(cel.layer(0).name() == "Layer 3");
        REQUIRE(cel.layer(1).name() == "Layer 1");
        REQUIRE(cel.layer(2).name() == "Layer 2");
    }

    SECTION("out-of-range indices are ignored") {
        cel.moveLayer(0, 5);
        REQUIRE(cel.layer(0).name() == "Layer 1");
    }

    SECTION("moving a layer to itself is a no-op") {
        cel.moveLayer(1, 1);
        REQUIRE(cel.layer(1).name() == "Layer 2");
    }
}

TEST_CASE("Cel exposure sheet maps frames to drawings", "[core][sheet]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L1");
    layer.addFrame();
    layer.addFrame();
    layer.addFrame();  // 動画3枚

    SECTION("default exposure is empty (-1)") {
        cut.setFrameCount(4);
        REQUIRE(cel.exposure(0) == -1);
        REQUIRE(cel.exposure(3) == -1);
        REQUIRE(cel.exposure(99) == -1);  // 範囲外も-1
    }

    SECTION("setExposure assigns a drawing to a frame") {
        cut.setFrameCount(4);
        cel.setExposure(2, 1);
        REQUIRE(cel.exposure(2) == 1);
        REQUIRE(cel.exposure(1) == -1);
    }

    SECTION("applyStepPattern: 2コマ打ち") {
        cut.setFrameCount(6);
        cel.applyStepPattern(2, 6);
        REQUIRE(cel.exposure(0) == 0);
        REQUIRE(cel.exposure(1) == 0);
        REQUIRE(cel.exposure(2) == 1);
        REQUIRE(cel.exposure(3) == 1);
        REQUIRE(cel.exposure(4) == 2);
        REQUIRE(cel.exposure(5) == 2);
    }

    SECTION("applyStepPattern can start after blank frames") {
        cut.setFrameCount(8);
        cel.setExposure(3, 0);
        cel.applyStepPattern(2, 8, 3);
        REQUIRE(cel.exposure(0) == -1);
        REQUIRE(cel.exposure(1) == -1);
        REQUIRE(cel.exposure(2) == -1);
        REQUIRE(cel.exposure(3) == 0);
        REQUIRE(cel.exposure(4) == 0);
        REQUIRE(cel.exposure(5) == 1);
        REQUIRE(cel.exposure(6) == 1);
        REQUIRE(cel.exposure(7) == 2);
    }

    SECTION("applyStepPattern: 動画が尽きたら以降は空欄") {
        cut.setFrameCount(8);
        cel.applyStepPattern(2, 8);  // 動画3枚×2コマ=6コマ分だけ割付
        REQUIRE(cel.exposure(5) == 2);
        REQUIRE(cel.exposure(6) == -1);
        REQUIRE(cel.exposure(7) == -1);
    }

    SECTION("setFrameCount(尺)の変更が全セルの露出表長に反映される") {
        cut.setFrameCount(10);
        REQUIRE(cel.exposures().size() == 10);
        cut.setFrameCount(2);
        REQUIRE(cel.exposures().size() == 2);
    }
}

TEST_CASE("Cel position keys interpolate linearly", "[core][tap]") {
    core::Cel cel("A");

    SECTION("no keys means origin") {
        const auto p = cel.positionAt(5);
        REQUIRE(p.x == 0.0f);
        REQUIRE(p.y == 0.0f);
    }

    SECTION("interpolation between keys (等速の引き)") {
        cel.setPositionKey(0, {0.0f, 0.0f});
        cel.setPositionKey(10, {100.0f, -50.0f});

        const auto mid = cel.positionAt(5);
        REQUIRE(mid.x == 50.0f);
        REQUIRE(mid.y == -25.0f);

        // キーの外側は端の値を維持
        REQUIRE(cel.positionAt(0).x == 0.0f);
        REQUIRE(cel.positionAt(10).x == 100.0f);
        REQUIRE(cel.positionAt(20).x == 100.0f);
    }

    SECTION("key removal") {
        cel.setPositionKey(0, {0.0f, 0.0f});
        cel.setPositionKey(10, {100.0f, 0.0f});
        cel.removePositionKey(10);
        REQUIRE(cel.positionAt(10).x == 0.0f);  // 残ったキーの値
    }
}

TEST_CASE("renderCutFrame applies cel position offset", "[core][tap][compositor]") {
    core::Cut cut("Cut 1");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");
    {
        core::Bitmap bitmap(8, 8);
        bitmap.fill({0, 0, 0, 0});
        bitmap.setPixel(2, 2, {0, 0, 0, 255});
        layer.addFrame().bitmap() = std::move(bitmap);
    }
    cut.setFrameCount(3);
    cel.setExposure(0, 0);
    cel.setExposure(1, 0);
    cel.setExposure(2, 0);
    cel.setPositionKey(0, {0.0f, 0.0f});
    cel.setPositionKey(2, {4.0f, 2.0f});  // 2コマで(4,2)移動 → コマ2で(2,1)

    const auto f0 = core::renderCutFrame(cut, 0, 8, 8);
    const auto f1 = core::renderCutFrame(cut, 1, 8, 8);
    const auto f2 = core::renderCutFrame(cut, 2, 8, 8);
    REQUIRE(f0.pixel(2, 2).r == 0);
    REQUIRE(f1.pixel(4, 3).r == 0);  // (2+2, 2+1)
    REQUIRE(f1.pixel(2, 2).r == 255);
    REQUIRE(f2.pixel(6, 4).r == 0);  // (2+4, 2+2)
}

TEST_CASE("Cel paper size defaults to 0 (canvas size) and is settable", "[core][paper]") {
    core::Cel cel("A");
    REQUIRE(cel.paperWidth() == 0);
    REQUIRE(cel.paperHeight() == 0);

    cel.setPaperSize(1920 * 2, 1080);
    REQUIRE(cel.paperWidth() == 3840);
    REQUIRE(cel.paperHeight() == 1080);
}

TEST_CASE("Cel::resizePaper moves non-empty bitmaps to the new size (centered)", "[core][paper]") {
    core::Cel cel("A");
    core::Layer& layer = cel.addLayer("L1");

    // 動画1: 8x8で(2,2)に目印(中心寄り)
    {
        core::Bitmap bitmap(8, 8);
        bitmap.fill({0, 0, 0, 0});
        bitmap.setPixel(2, 2, {255, 0, 0, 255});
        layer.addFrame().bitmap() = std::move(bitmap);
    }
    // 動画2: 空コマ(未描画)。リサイズ後も空のまま維持されるはず
    layer.addFrame();

    SECTION("enlarging keeps the drawing centered and pads with transparency") {
        cel.resizePaper(16, 16);
        REQUIRE(cel.paperWidth() == 16);
        REQUIRE(cel.paperHeight() == 16);

        const core::Bitmap& b0 = layer.frame(0).bitmap();
        REQUIRE(b0.width() == 16);
        REQUIRE(b0.height() == 16);
        // 8x8→16x16への中央寄せは+4オフセット: (2,2)→(6,6)
        const auto marked = b0.pixel(6, 6);
        REQUIRE(marked.r == 255);
        REQUIRE(marked.a == 255);
        // 余白は透明
        REQUIRE(b0.pixel(0, 0).a == 0);

        // 空コマはそのまま空(未描画)
        REQUIRE(layer.frame(1).bitmap().isEmpty());
    }

    SECTION("shrinking clips content outside the new bounds") {
        // 8x8→4x4: オフセット(4-8)/2=-2。 元(2,2)は(0,0)へ。元(6,6)ならクリップされる
        core::Bitmap bitmap(8, 8);
        bitmap.fill({0, 0, 0, 0});
        bitmap.setPixel(6, 6, {0, 255, 0, 255});  // 端寄りの点(縮小でクリップされるはず)
        layer.frame(0).bitmap() = std::move(bitmap);

        cel.resizePaper(4, 4);
        const core::Bitmap& b0 = layer.frame(0).bitmap();
        REQUIRE(b0.width() == 4);
        REQUIRE(b0.height() == 4);
        // (6,6)は新しい4x4の範囲外になりクリップされる: 4x4全域が透明のまま
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) REQUIRE(b0.pixel(x, y).a == 0);
        }
    }
}

TEST_CASE("Cel paper size round trips through ppam", "[core][paper][io]") {
    core::Project project("P");
    core::Cut& cut = project.addScene("S").addCut("C");
    core::Cel& cel = cut.addCel("A");
    core::Layer& layer = cel.addLayer("L");
    layer.addFrame().bitmap() = core::Bitmap(4, 4);
    cel.resizePaper(3840, 1080);  // 横2倍の引きセル

    core::Cel& celDefault = cut.addCel("B");  // 既定(0=キャンバスサイズ)のセルも往復確認する
    celDefault.addLayer("L").addFrame();

    const auto path = std::filesystem::temp_directory_path() / "ppam_paper_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const core::Cel& loadedCel = loaded->scene(0).cut(0).cel(0);
    REQUIRE(loadedCel.paperWidth() == 3840);
    REQUIRE(loadedCel.paperHeight() == 1080);

    const core::Cel& loadedDefault = loaded->scene(0).cut(0).cel(1);
    REQUIRE(loadedDefault.paperWidth() == 0);
    REQUIRE(loadedDefault.paperHeight() == 0);

    std::filesystem::remove_all(path);
}

TEST_CASE("Cut::moveCel reorders cels", "[core]") {
    core::Cut cut("Cut 1");
    cut.addCel("Cel A");
    cut.addCel("Cel B");
    cut.addCel("Cel C");

    cut.moveCel(0, 2);
    REQUIRE(cut.cel(0).name() == "Cel B");
    REQUIRE(cut.cel(1).name() == "Cel C");
    REQUIRE(cut.cel(2).name() == "Cel A");
}

TEST_CASE("Scene::moveCut reorders cuts", "[core]") {
    core::Scene scene("Scene 1");
    scene.addCut("Cut A");
    scene.addCut("Cut B");
    scene.addCut("Cut C");

    SECTION("moves a cut to a later position") {
        scene.moveCut(0, 2);
        REQUIRE(scene.cut(0).name() == "Cut B");
        REQUIRE(scene.cut(1).name() == "Cut C");
        REQUIRE(scene.cut(2).name() == "Cut A");
    }

    SECTION("out-of-range indices are ignored") {
        scene.moveCut(0, 5);
        scene.moveCut(5, 0);
        scene.moveCut(1, 1);
        REQUIRE(scene.cut(0).name() == "Cut A");
        REQUIRE(scene.cut(1).name() == "Cut B");
        REQUIRE(scene.cut(2).name() == "Cut C");
    }
}

TEST_CASE("Cut status defaults to NotStarted and is settable", "[core]") {
    core::Cut cut("Cut 1");
    REQUIRE(cut.status() == core::CutStatus::NotStarted);

    cut.setStatus(core::CutStatus::KeyAnimation);
    REQUIRE(cut.status() == core::CutStatus::KeyAnimation);
}

TEST_CASE("Previz model and camera keys interpolate", "[core][previz]") {
    core::PrevizModel model;
    model.transformKeys[0] = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}};
    model.transformKeys[10] = {{10, 0, -20}, {0, 90, 0}, {1, 1, 1}};

    const auto mid = model.transformAt(5);
    REQUIRE(mid.position.x == 5.0f);
    REQUIRE(mid.position.z == -10.0f);
    REQUIRE(mid.rotationDeg.y == 45.0f);

    model.filePath = ":humanoid";
    model.poseKeys[0].leftShoulderPitchDeg = -30.0f;
    model.poseKeys[10].leftShoulderPitchDeg = 30.0f;
    model.poseKeys[0].rightKneeDeg = 10.0f;
    model.poseKeys[10].rightKneeDeg = 50.0f;
    REQUIRE(model.poseAt(5).leftShoulderPitchDeg == 0.0f);
    REQUIRE(model.poseAt(5).rightKneeDeg == 30.0f);

    core::PrevizCamera camera;
    camera.state.focalLengthMm = 50.0f;
    // 50mm/フルサイズ36mm → 水平画角約39.6度
    REQUIRE(camera.horizontalFovDeg(0) > 39.0f);
    REQUIRE(camera.horizontalFovDeg(0) < 40.5f);

    // 望遠(100mm)にすると画角が狭まる=パース圧縮
    camera.keys[0] = {{0, 0, 5}, {}, 50.0f};
    camera.keys[10] = {{0, 0, 5}, {}, 100.0f};
    REQUIRE(camera.horizontalFovDeg(10) < 21.0f);
    REQUIRE(camera.stateAt(5).focalLengthMm == 75.0f);  // 焦点距離も補間される
}

TEST_CASE("Previz scene round trips through ppam", "[core][previz][io]") {
    core::Project project("P");
    core::Cut& cut = project.addScene("S").addCut("C");
    cut.addCel("A").addLayer("L").addFrame();  // 最小構成

    core::PrevizModel model;
    model.name = "キャラA";
    model.filePath = "C:/models/chara.glb";
    model.transform.position = {1, 2, 3};
    model.transformKeys[0] = {{0, 0, 0}, {0, 0, 0}, {1, 1, 1}};
    model.transformKeys[8] = {{5, 0, 0}, {0, 180, 0}, {2, 2, 2}};
    model.filePath = ":humanoid";
    model.humanoidPose.headYawDeg = 12.0f;
    model.humanoidBody.chestWidth = 1.25f;
    model.humanoidBody.armLength = 1.40f;
    model.humanoidBody.legThickness = 0.70f;
    model.humanoidBody.footScale = 1.30f;
    model.poseKeys[0].leftHipPitchDeg = -20.0f;
    model.poseKeys[8].leftHipPitchDeg = 20.0f;
    cut.previz().models.push_back(model);
    cut.previz().camera.state.focalLengthMm = 85.0f;
    cut.previz().camera.sensorWidthMm = 36.0f;
    cut.previz().camera.keys[3] = {{0, 1, 4}, {10, 0, 0}, 85.0f};

    const auto path = std::filesystem::temp_directory_path() / "ppam_previz_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const core::PrevizScene& previz = loaded->scene(0).cut(0).previz();
    REQUIRE(previz.models.size() == 1);
    REQUIRE(previz.models[0].name == "キャラA");
    REQUIRE(previz.models[0].filePath == ":humanoid");
    REQUIRE(previz.models[0].transformKeys.size() == 2);
    REQUIRE(previz.models[0].transformAt(4).position.x == 2.5f);
    REQUIRE(previz.models[0].humanoidPose.headYawDeg == 12.0f);
    REQUIRE(previz.models[0].humanoidBody.chestWidth == 1.25f);
    REQUIRE(previz.models[0].humanoidBody.armLength == 1.40f);
    REQUIRE(previz.models[0].humanoidBody.legThickness == 0.70f);
    REQUIRE(previz.models[0].humanoidBody.footScale == 1.30f);
    REQUIRE(previz.models[0].poseKeys.size() == 2);
    REQUIRE(previz.models[0].poseAt(4).leftHipPitchDeg == 0.0f);
    REQUIRE(previz.camera.state.focalLengthMm == 85.0f);
    REQUIRE(previz.camera.keys.size() == 1);
    REQUIRE(previz.camera.keys.at(3).rotationDeg.x == 10.0f);

    std::filesystem::remove_all(path);
}

TEST_CASE("Cut camera frame keys interpolate", "[core][camera]") {
    core::Cut cut("Cut 1");

    SECTION("no keys means nullopt") {
        REQUIRE_FALSE(cut.cameraFrameAt(5).has_value());
    }

    SECTION("single key always returns that value") {
        cut.setCameraKey(3, {{100.0f, 50.0f}, 0.8});
        REQUIRE(cut.cameraFrameAt(0)->center.x == 100.0f);
        REQUIRE(cut.cameraFrameAt(3)->center.y == 50.0f);
        REQUIRE(cut.cameraFrameAt(999)->scale == 0.8);
    }

    SECTION("two keys interpolate linearly and clamp outside range") {
        cut.setCameraKey(0, {{0.0f, 0.0f}, 1.0});
        cut.setCameraKey(24, {{240.0f, 120.0f}, 0.5});

        const auto mid = cut.cameraFrameAt(12);
        REQUIRE(mid.has_value());
        REQUIRE(mid->center.x == 120.0f);
        REQUIRE(mid->center.y == 60.0f);
        REQUIRE(mid->scale == 0.75);

        REQUIRE(cut.cameraFrameAt(0)->center.x == 0.0f);        // 最初のキー上
        REQUIRE(cut.cameraFrameAt(24)->center.x == 240.0f);     // 最後のキー上
        REQUIRE(cut.cameraFrameAt(100)->center.x == 240.0f);    // 範囲外(後)はクランプ
    }

    SECTION("scale is clamped to a minimum of 0.05") {
        cut.setCameraKey(0, {{0.0f, 0.0f}, 0.01});
        REQUIRE(cut.cameraFrameAt(0)->scale == 0.05);
    }
}

TEST_CASE("Cut camera keys round trip through ppam", "[core][camera][io]") {
    core::Project project("P");
    core::Cut& cut = project.addScene("S").addCut("C");
    cut.addCel("A").addLayer("L").addFrame();  // 最小構成

    cut.setCameraKey(0, {{100.0f, 200.0f}, 1.0});
    cut.setCameraKey(24, {{50.0f, 60.0f}, 0.5});

    const auto path = std::filesystem::temp_directory_path() / "ppam_camera_test.ppproj";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const core::Cut& loadedCut = loaded->scene(0).cut(0);
    REQUIRE(loadedCut.cameraKeys().size() == 2);
    REQUIRE(loadedCut.cameraFrameAt(0)->center.x == 100.0f);
    REQUIRE(loadedCut.cameraFrameAt(0)->center.y == 200.0f);
    REQUIRE(loadedCut.cameraFrameAt(24)->center.x == 50.0f);
    REQUIRE(loadedCut.cameraFrameAt(24)->center.y == 60.0f);
    REQUIRE(loadedCut.cameraFrameAt(24)->scale == 0.5);

    std::filesystem::remove_all(path);
}

TEST_CASE("Project supports multiple scenes and cuts", "[core]") {
    core::Project project;

    project.addScene("Scene A");
    core::Scene& sceneB = project.addScene("Scene B");
    sceneB.addCut("Cut 1");
    sceneB.addCut("Cut 2");

    REQUIRE(project.sceneCount() == 2);
    REQUIRE(project.scene(1).cutCount() == 2);
}
