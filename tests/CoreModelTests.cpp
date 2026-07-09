#include <catch2/catch_test_macros.hpp>

#include "core/Project.h"

TEST_CASE("Project owns Scene -> Cut -> Layer -> Frame hierarchy", "[core]") {
    core::Project project("Test Project");

    core::Scene& scene = project.addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut 1");
    core::Layer& layer = cut.addLayer("Layer 1");
    core::Frame& frame = layer.addFrame();

    SECTION("hierarchy is reachable via index accessors") {
        REQUIRE(project.sceneCount() == 1);
        REQUIRE(project.scene(0).name() == "Scene 1");
        REQUIRE(project.scene(0).cutCount() == 1);
        REQUIRE(project.scene(0).cut(0).name() == "Cut 1");
        REQUIRE(project.scene(0).cut(0).layerCount() == 1);
        REQUIRE(project.scene(0).cut(0).layer(0).name() == "Layer 1");
        REQUIRE(project.scene(0).cut(0).layer(0).frameCount() == 1);
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

    (void)cut;
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
