#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "core/ProjectIO.h"

namespace {

std::filesystem::path tempFile(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

core::Project makeSampleProject() {
    core::Project project("Sample");
    core::Scene& scene = project.addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut A");
    core::Cel& cel = cut.addCel("Cel A");
    cel.setVisible(false);  // 属性の往復も確認する
    core::Layer& layer = cel.addLayer("Layer 1");
    layer.setVisible(false);

    core::Frame& f0 = layer.addFrame();
    f0.bitmap() = core::Bitmap(16, 8);
    f0.bitmap().fill({255, 255, 255, 255});
    f0.bitmap().setPixel(3, 4, {10, 20, 30, 255});

    layer.addFrame();  // 空ビットマップのフレーム

    core::Frame& f2 = layer.addFrame();
    f2.bitmap() = core::Bitmap(4, 4);
    f2.bitmap().fill({1, 2, 3, 4});

    project.addScene("Scene 2").addCut("Cut B");
    return project;
}

}  // namespace

TEST_CASE("ProjectIO round trip preserves structure and pixels", "[core][io]") {
    const auto path = tempFile("ppam_roundtrip_test.ppam");
    const core::Project original = makeSampleProject();

    std::string error;
    REQUIRE(core::ProjectIO::save(original, path, &error));

    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    REQUIRE(loaded->name() == "Sample");
    REQUIRE(loaded->sceneCount() == 2);
    REQUIRE(loaded->scene(0).name() == "Scene 1");
    REQUIRE(loaded->scene(1).cutCount() == 1);
    REQUIRE(loaded->scene(1).cut(0).name() == "Cut B");

    REQUIRE(loaded->scene(0).cut(0).celCount() == 1);
    const core::Cel& cel = loaded->scene(0).cut(0).cel(0);
    REQUIRE(cel.name() == "Cel A");
    REQUIRE_FALSE(cel.visible());

    const core::Layer& layer = cel.layer(0);
    REQUIRE(layer.name() == "Layer 1");
    REQUIRE_FALSE(layer.visible());
    REQUIRE(layer.frameCount() == 3);

    const core::Bitmap& b0 = layer.frame(0).bitmap();
    REQUIRE(b0.width() == 16);
    REQUIRE(b0.height() == 8);
    const auto marked = b0.pixel(3, 4);
    REQUIRE(marked.r == 10);
    REQUIRE(marked.g == 20);
    REQUIRE(marked.b == 30);
    REQUIRE(b0.pixel(0, 0).r == 255);

    REQUIRE(layer.frame(1).bitmap().isEmpty());

    const core::Bitmap& b2 = layer.frame(2).bitmap();
    REQUIRE(b2.width() == 4);
    REQUIRE(b2.pixel(2, 2).a == 4);

    std::filesystem::remove(path);
}

TEST_CASE("ProjectIO load reports errors for invalid files", "[core][io]") {
    std::string error;

    SECTION("nonexistent file") {
        REQUIRE(core::ProjectIO::load(tempFile("ppam_missing_xyz.ppam"), &error) == nullptr);
        REQUIRE_FALSE(error.empty());
    }

    SECTION("wrong magic") {
        const auto path = tempFile("ppam_badmagic.ppam");
        std::ofstream(path, std::ios::binary) << "NOPE0000000000000000";
        REQUIRE(core::ProjectIO::load(path, &error) == nullptr);
        REQUIRE(error.find("プロジェクトファイルではありません") != std::string::npos);
        std::filesystem::remove(path);
    }

    SECTION("future container version") {
        const auto path = tempFile("ppam_future.ppam");
        {
            std::ofstream out(path, std::ios::binary);
            out.write("PPAM", 4);
            const uint32_t version = 999;
            const uint64_t jsonSize = 0;
            out.write(reinterpret_cast<const char*>(&version), 4);
            out.write(reinterpret_cast<const char*>(&jsonSize), 8);
        }
        REQUIRE(core::ProjectIO::load(path, &error) == nullptr);
        REQUIRE(error.find("新しいバージョン") != std::string::npos);
        std::filesystem::remove(path);
    }

    SECTION("truncated file") {
        const auto path = tempFile("ppam_truncated.ppam");
        core::Project project = makeSampleProject();
        REQUIRE(core::ProjectIO::save(project, path, &error));
        // 末尾40%を切り落とす
        const auto fullSize = std::filesystem::file_size(path);
        std::filesystem::resize_file(path, fullSize * 6 / 10);
        REQUIRE(core::ProjectIO::load(path, &error) == nullptr);
        REQUIRE_FALSE(error.empty());
        std::filesystem::remove(path);
    }
}
