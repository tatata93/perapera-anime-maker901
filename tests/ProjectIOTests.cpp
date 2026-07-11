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
    cut.setAction("走って逃げる");     // 絵コンテメモの往復も確認する
    cut.setDialogue("待って!");
    core::Cel& cel = cut.addCel("Cel A");
    cel.setVisible(false);  // 属性の往復も確認する
    core::Layer& layer = cel.addLayer("Layer 1");
    layer.setVisible(false);

    core::Layer& colorTraceLayer = cel.addLayer("Layer 2");
    colorTraceLayer.setRole(core::LayerRole::ColorTrace);

    core::Layer& correctionLayer = cel.addLayer("Layer 3");
    correctionLayer.setRole(core::LayerRole::Correction);

    core::Frame& f0 = layer.addFrame();
    f0.bitmap() = core::Bitmap(16, 8);
    f0.bitmap().fill({255, 255, 255, 255});
    f0.bitmap().setPixel(3, 4, {10, 20, 30, 255});

    layer.addFrame();  // 空ビットマップのフレーム

    core::Frame& f2 = layer.addFrame();
    f2.bitmap() = core::Bitmap(4, 4);
    f2.bitmap().fill({1, 2, 3, 4});

    project.addScene("Scene 2").addCut("Cut B");

    project.palette().push_back({255, 0, 0, 255});
    project.palette().push_back({0, 255, 0, 128});
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
    REQUIRE(loaded->scene(1).cut(0).action().empty());   // 未設定は空文字のまま
    REQUIRE(loaded->scene(1).cut(0).dialogue().empty());

    REQUIRE(loaded->scene(0).cut(0).action() == "走って逃げる");
    REQUIRE(loaded->scene(0).cut(0).dialogue() == "待って!");

    REQUIRE(loaded->scene(0).cut(0).celCount() == 1);
    const core::Cel& cel = loaded->scene(0).cut(0).cel(0);
    REQUIRE(cel.name() == "Cel A");
    REQUIRE_FALSE(cel.visible());

    REQUIRE(cel.layerCount() == 3);
    const core::Layer& layer = cel.layer(0);
    REQUIRE(layer.name() == "Layer 1");
    REQUIRE_FALSE(layer.visible());
    REQUIRE(layer.role() == core::LayerRole::Normal);
    REQUIRE(layer.frameCount() == 3);

    REQUIRE(cel.layer(1).name() == "Layer 2");
    REQUIRE(cel.layer(1).role() == core::LayerRole::ColorTrace);

    REQUIRE(cel.layer(2).name() == "Layer 3");
    REQUIRE(cel.layer(2).role() == core::LayerRole::Correction);

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

    REQUIRE(loaded->palette().size() == 2);
    REQUIRE(loaded->palette()[0].r == 255);
    REQUIRE(loaded->palette()[0].g == 0);
    REQUIRE(loaded->palette()[0].b == 0);
    REQUIRE(loaded->palette()[0].a == 255);
    REQUIRE(loaded->palette()[1].r == 0);
    REQUIRE(loaded->palette()[1].g == 255);
    REQUIRE(loaded->palette()[1].b == 0);
    REQUIRE(loaded->palette()[1].a == 128);

    std::filesystem::remove(path);
}

TEST_CASE("Multiplane setup round trips through ppam", "[core][io][multiplane]") {
    const auto path = tempFile("ppam_multiplane_roundtrip_test.ppam");

    core::Project project("MP");
    core::Scene& scene = project.addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A");
    cut.addCel("B");

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.camera.focalLengthMm = 35.0;
    mp.camera.sensorWidthMm = 24.0;
    mp.camera.apertureFStop = 2.8;
    mp.camera.focusDistanceMm = 600.0;
    mp.samplesPerPixel = 16;
    mp.planes.push_back({0, 500.0, 400.0});
    mp.planes.push_back({1, 300.0, 300.0});

    // 比較用: マルチプレーンを持たないカット(既定値のまま)も一緒に往復させ、省略時の既定を確認する
    scene.addCut("Cut B");

    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const core::MultiplaneSetup& loadedMp = loaded->scene(0).cut(0).multiplane();
    REQUIRE(loadedMp.enabled == true);
    REQUIRE(loadedMp.camera.focalLengthMm == 35.0);
    REQUIRE(loadedMp.camera.sensorWidthMm == 24.0);
    REQUIRE(loadedMp.camera.apertureFStop == 2.8);
    REQUIRE(loadedMp.camera.focusDistanceMm == 600.0);
    REQUIRE(loadedMp.samplesPerPixel == 16);
    REQUIRE(loadedMp.planes.size() == 2);
    REQUIRE(loadedMp.planes[0].celIndex == 0);
    REQUIRE(loadedMp.planes[0].distanceMm == 500.0);
    REQUIRE(loadedMp.planes[0].widthMm == 400.0);
    REQUIRE(loadedMp.planes[1].celIndex == 1);
    REQUIRE(loadedMp.planes[1].distanceMm == 300.0);
    REQUIRE(loadedMp.planes[1].widthMm == 300.0);

    // マルチプレーン設定を持たないカットは既定(無効・段なし)のまま
    const core::MultiplaneSetup& defaultMp = loaded->scene(0).cut(1).multiplane();
    REQUIRE_FALSE(defaultMp.enabled);
    REQUIRE(defaultMp.planes.empty());

    std::filesystem::remove(path);
}

TEST_CASE("Storyboard panels round trip through ppam", "[core][io][storyboard]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    scene.addCut("C").addCel("A").addLayer("L").addFrame();

    core::StoryboardPanel panel1;
    panel1.cutLabel = "1";
    panel1.action = "少年が走り出す";
    panel1.dialogue = "行くぞ!";
    panel1.durationFrames = 36;
    panel1.drawing = core::Bitmap(16, 9);
    panel1.drawing.fill({0, 0, 0, 0});
    panel1.drawing.setPixel(3, 3, {200, 30, 30, 255});
    scene.storyboard().push_back(std::move(panel1));

    core::StoryboardPanel panel2;  // 同じカット番号の2コマ目(絵は未描画)
    panel2.cutLabel = "1";
    panel2.durationFrames = 12;
    scene.storyboard().push_back(std::move(panel2));

    const auto path = std::filesystem::temp_directory_path() / "ppam_storyboard_test.ppam";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& sb = loaded->scene(0).storyboard();
    REQUIRE(sb.size() == 2);
    REQUIRE(sb[0].cutLabel == "1");
    REQUIRE(sb[0].action == "少年が走り出す");
    REQUIRE(sb[0].dialogue == "行くぞ!");
    REQUIRE(sb[0].durationFrames == 36);
    REQUIRE(sb[0].drawing.width() == 16);
    REQUIRE(sb[0].drawing.pixel(3, 3).r == 200);
    REQUIRE(sb[1].cutLabel == "1");
    REQUIRE(sb[1].durationFrames == 12);
    REQUIRE(sb[1].drawing.isEmpty());

    std::filesystem::remove(path);
}

TEST_CASE("Setting boards round trip through ppam", "[core][io][settingboard]") {
    core::Project project("P");
    project.addScene("S").addCut("C").addCel("A").addLayer("L").addFrame();

    core::SettingBoard board1;
    board1.name = "キャラ: 主人公";
    board1.image = core::Bitmap(16, 9);
    board1.image.fill({0, 0, 0, 0});
    board1.image.setPixel(5, 2, {10, 200, 40, 255});  // 目印ピクセル
    project.settingBoards().push_back(std::move(board1));

    // 色指定(色指定書)2色
    core::ColorSpec spec1;
    spec1.name = "肌";
    spec1.color = {255, 224, 196, 255};
    core::ColorSpec spec2;
    spec2.name = "肌 影";
    spec2.color = {233, 183, 150, 255};
    project.settingBoards()[0].colorSpecs.push_back(spec1);
    project.settingBoards()[0].colorSpecs.push_back(spec2);

    core::SettingBoard board2;  // 空画像のボード
    board2.name = "美術: 教室";
    project.settingBoards().push_back(std::move(board2));

    const auto path = std::filesystem::temp_directory_path() / "ppam_settingboard_test.ppam";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    const auto& boards = loaded->settingBoards();
    REQUIRE(boards.size() == 2);
    REQUIRE(boards[0].name == "キャラ: 主人公");
    REQUIRE(boards[0].image.width() == 16);
    REQUIRE(boards[0].image.height() == 9);
    const auto marked = boards[0].image.pixel(5, 2);
    REQUIRE(marked.r == 10);
    REQUIRE(marked.g == 200);
    REQUIRE(marked.b == 40);
    REQUIRE(boards[1].name == "美術: 教室");
    REQUIRE(boards[1].image.isEmpty());

    // 色指定の往復確認
    REQUIRE(boards[0].colorSpecs.size() == 2);
    REQUIRE(boards[0].colorSpecs[0].name == "肌");
    REQUIRE(boards[0].colorSpecs[0].color.r == 255);
    REQUIRE(boards[0].colorSpecs[0].color.g == 224);
    REQUIRE(boards[0].colorSpecs[0].color.b == 196);
    REQUIRE(boards[0].colorSpecs[1].name == "肌 影");
    REQUIRE(boards[0].colorSpecs[1].color.r == 233);
    REQUIRE(boards[0].colorSpecs[1].color.g == 183);
    REQUIRE(boards[0].colorSpecs[1].color.b == 150);
    REQUIRE(boards[1].colorSpecs.empty());

    std::filesystem::remove(path);
}

TEST_CASE("Cut status round trips through ppam", "[core][io][edit]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cutA = scene.addCut("Cut A");
    cutA.addCel("A").addLayer("L").addFrame();  // 最小構成
    cutA.setStatus(core::CutStatus::Finishing);

    core::Cut& cutB = scene.addCut("Cut B");  // 未設定(既定のNotStartedのまま)のカットも往復確認する
    cutB.addCel("A").addLayer("L").addFrame();

    const auto path = std::filesystem::temp_directory_path() / "ppam_cutstatus_test.ppam";
    std::string error;
    REQUIRE(core::ProjectIO::save(project, path, &error));
    const auto loaded = core::ProjectIO::load(path, &error);
    REQUIRE(loaded != nullptr);

    REQUIRE(loaded->scene(0).cut(0).status() == core::CutStatus::Finishing);
    REQUIRE(loaded->scene(0).cut(1).status() == core::CutStatus::NotStarted);

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
