#include <catch2/catch_test_macros.hpp>

#include "core/Project.h"

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

TEST_CASE("Project supports multiple scenes and cuts", "[core]") {
    core::Project project;

    project.addScene("Scene A");
    core::Scene& sceneB = project.addScene("Scene B");
    sceneB.addCut("Cut 1");
    sceneB.addCut("Cut 2");

    REQUIRE(project.sceneCount() == 2);
    REQUIRE(project.scene(1).cutCount() == 2);
}
