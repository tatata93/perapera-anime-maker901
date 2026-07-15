#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "core/ProjectIO.h"

namespace {

std::filesystem::path tempFolder(const char* name) {
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
    cel.setOpacity(0.5);
    core::Layer& layer = cel.addLayer("Layer 1");
    layer.setVisible(false);
    layer.setOpacity(0.25);

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
    const auto folder = tempFolder("ppam_roundtrip_test.ppproj");
    const core::Project original = makeSampleProject();

    std::string error;
    REQUIRE(core::ProjectIO::save(original, folder, &error));

    const auto loaded = core::ProjectIO::load(folder, &error);
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
    REQUIRE(cel.opacity() == 0.5);

    REQUIRE(cel.layerCount() == 3);
    const core::Layer& layer = cel.layer(0);
    REQUIRE(layer.name() == "Layer 1");
    REQUIRE_FALSE(layer.visible());
    REQUIRE(layer.opacity() == 0.25);
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

    std::filesystem::remove_all(folder);
}

TEST_CASE("Multiplane setup round trips through ppproj", "[core][io][multiplane]") {
    const auto folder = tempFolder("ppam_multiplane_roundtrip_test.ppproj");

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
    mp.exportSamplesPerPixel = 128;
    mp.planes.push_back({0, 500.0, 400.0});
    mp.planes.push_back({1, 300.0, 300.0});
    mp.framingLock = false;  // フレーミング固定(既定true)も往復させる
    mp.framingWidthMm = 480.0;
    mp.framingRefDistanceMm = 700.0;

    // 透過光(T光)複数灯も往復させる
    core::MultiplaneBacklight light1;
    light1.name = "灯1";
    light1.enabled = true;
    light1.intensity = 6.0;
    light1.colorR = 1.0;
    light1.colorG = 0.5;
    light1.colorB = 0.25;
    light1.paintTransmittance = 0.3;
    light1.bloomRadiusPx = 32.0;
    light1.bloomStrength = 0.7;
    mp.backlights.push_back(light1);

    core::MultiplaneBacklight light2;
    light2.name = "灯2";
    light2.enabled = false;  // 無効な灯も設定ごと往復させる
    light2.intensity = 3.0;
    light2.colorR = 0.2;
    light2.colorG = 0.8;
    light2.colorB = 0.9;
    mp.backlights.push_back(light2);

    // 比較用: マルチプレーンを持たないカット(既定値のまま)も一緒に往復させ、省略時の既定を確認する
    scene.addCut("Cut B");

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);

    const core::MultiplaneSetup& loadedMp = loaded->scene(0).cut(0).multiplane();
    REQUIRE(loadedMp.enabled == true);
    REQUIRE(loadedMp.camera.focalLengthMm == 35.0);
    REQUIRE(loadedMp.camera.sensorWidthMm == 24.0);
    REQUIRE(loadedMp.camera.apertureFStop == 2.8);
    REQUIRE(loadedMp.camera.focusDistanceMm == 600.0);
    REQUIRE(loadedMp.samplesPerPixel == 16);
    REQUIRE(loadedMp.exportSamplesPerPixel == 128);
    REQUIRE(loadedMp.planes.size() == 2);
    REQUIRE(loadedMp.planes[0].celIndex == 0);
    REQUIRE(loadedMp.planes[0].distanceMm == 500.0);
    REQUIRE(loadedMp.planes[0].widthMm == 400.0);
    REQUIRE(loadedMp.planes[1].celIndex == 1);
    REQUIRE(loadedMp.planes[1].distanceMm == 300.0);
    REQUIRE(loadedMp.planes[1].widthMm == 300.0);
    REQUIRE_FALSE(loadedMp.framingLock);
    REQUIRE(loadedMp.framingWidthMm == 480.0);
    REQUIRE(loadedMp.framingRefDistanceMm == 700.0);

    REQUIRE(loadedMp.backlights.size() == 2);
    REQUIRE(loadedMp.backlights[0].name == "灯1");
    REQUIRE(loadedMp.backlights[0].enabled == true);
    REQUIRE(loadedMp.backlights[0].intensity == 6.0);
    REQUIRE(loadedMp.backlights[0].colorR == 1.0);
    REQUIRE(loadedMp.backlights[0].colorG == 0.5);
    REQUIRE(loadedMp.backlights[0].colorB == 0.25);
    REQUIRE(loadedMp.backlights[0].paintTransmittance == 0.3);
    REQUIRE(loadedMp.backlights[0].bloomRadiusPx == 32.0);
    REQUIRE(loadedMp.backlights[0].bloomStrength == 0.7);
    REQUIRE(loadedMp.backlights[1].name == "灯2");
    REQUIRE(loadedMp.backlights[1].enabled == false);
    REQUIRE(loadedMp.backlights[1].intensity == 3.0);
    REQUIRE(loadedMp.backlights[1].colorR == 0.2);
    REQUIRE(loadedMp.backlights[1].colorG == 0.8);
    REQUIRE(loadedMp.backlights[1].colorB == 0.9);

    // マルチプレーン設定を持たないカットは既定(無効・段なし・灯なし・フレーミング既定)のまま
    const core::MultiplaneSetup& defaultMp = loaded->scene(0).cut(1).multiplane();
    REQUIRE_FALSE(defaultMp.enabled);
    REQUIRE(defaultMp.planes.empty());
    REQUIRE(defaultMp.backlights.empty());
    REQUIRE(defaultMp.framingLock);
    REQUIRE(defaultMp.framingWidthMm == 360.0);
    REQUIRE(defaultMp.framingRefDistanceMm == 500.0);

    std::filesystem::remove_all(folder);
}

TEST_CASE("Multiplane backlight mask/cel-mask and keyframe curves round trip through ppproj",
          "[core][io][multiplane][mask]") {
    const auto folder = tempFolder("ppam_backlight_mask_roundtrip_test.ppproj");

    core::Project project("MPMask");
    core::Scene& scene = project.addScene("Scene 1");
    core::Cut& cut = scene.addCut("Cut A");
    cut.addCel("A");
    cut.addCel("B");

    core::MultiplaneSetup& mp = cut.multiplane();
    mp.enabled = true;
    mp.planes.push_back({0, 500.0, 400.0});

    core::MultiplaneBacklight bl;
    bl.enabled = true;
    bl.intensity = 5.0;

    // 光源マスク(ペンで塗った範囲): 4x4のグレースケール、alphaが位置ごとに変わるパターン
    core::Bitmap mask(4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            mask.setPixel(x, y, {200, 100, 50, static_cast<uint8_t>((x + y * 4) * 16)});
        }
    }
    bl.mask = mask;
    bl.maskCelIndex = 1;
    bl.maskLayerIndex = 2;

    // この灯だけの点滅キー
    bl.intensityKeys = {{0, 0.0}, {1, 8.0}, {2, 0.0}};
    mp.backlights.push_back(bl);

    // コマ→値のキーフレーム曲線(滑らかなカメラ変化・絞り)
    mp.focalKeys = {{0, 35.0}, {5, 85.0}};
    mp.focusKeys = {{0, 300.0}, {5, 900.0}};
    mp.fstopKeys = {{0, 0.0}, {5, 2.8}};

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);

    const core::MultiplaneSetup& loadedMp = loaded->scene(0).cut(0).multiplane();
    REQUIRE(loadedMp.backlights.size() == 1);
    const core::MultiplaneBacklight& loadedBl = loadedMp.backlights[0];
    REQUIRE(loadedBl.enabled == true);
    REQUIRE_FALSE(loadedBl.mask.isEmpty());
    REQUIRE(loadedBl.mask.width() == 4);
    REQUIRE(loadedBl.mask.height() == 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const core::Bitmap::Pixel p = loadedBl.mask.pixel(x, y);
            REQUIRE(p.r == 200);
            REQUIRE(p.g == 100);
            REQUIRE(p.b == 50);
            REQUIRE(p.a == static_cast<uint8_t>((x + y * 4) * 16));
        }
    }
    REQUIRE(loadedBl.maskCelIndex == 1);
    REQUIRE(loadedBl.maskLayerIndex == 2);

    REQUIRE(loadedBl.intensityKeys.size() == 3);
    REQUIRE(loadedBl.intensityKeys.at(0) == 0.0);
    REQUIRE(loadedBl.intensityKeys.at(1) == 8.0);
    REQUIRE(loadedBl.intensityKeys.at(2) == 0.0);
    REQUIRE(loadedMp.focalKeys.size() == 2);
    REQUIRE(loadedMp.focalKeys.at(0) == 35.0);
    REQUIRE(loadedMp.focalKeys.at(5) == 85.0);
    REQUIRE(loadedMp.focusKeys.size() == 2);
    REQUIRE(loadedMp.focusKeys.at(0) == 300.0);
    REQUIRE(loadedMp.focusKeys.at(5) == 900.0);
    REQUIRE(loadedMp.fstopKeys.size() == 2);
    REQUIRE(loadedMp.fstopKeys.at(0) == 0.0);
    REQUIRE(loadedMp.fstopKeys.at(5) == 2.8);

    // マスク/セルマスク/キーを持たないカットは既定(空・-1)のまま
    core::Cut& cutB = scene.addCut("Cut B");
    cutB.multiplane().enabled = true;
    core::MultiplaneBacklight defaultBl;
    defaultBl.enabled = true;  // 有効だがmask/maskCel/keysは未設定
    cutB.multiplane().backlights.push_back(defaultBl);
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded2 = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded2 != nullptr);
    const core::MultiplaneSetup& defaultMp = loaded2->scene(0).cut(1).multiplane();
    REQUIRE(defaultMp.backlights.size() == 1);
    REQUIRE(defaultMp.backlights[0].mask.isEmpty());
    REQUIRE(defaultMp.backlights[0].maskCelIndex == -1);
    REQUIRE(defaultMp.backlights[0].maskLayerIndex == -1);
    REQUIRE(defaultMp.backlights[0].intensityKeys.empty());
    REQUIRE(defaultMp.focalKeys.empty());
    REQUIRE(defaultMp.focusKeys.empty());
    REQUIRE(defaultMp.fstopKeys.empty());

    std::filesystem::remove_all(folder);
}

TEST_CASE("Storyboard panels round trip through ppproj", "[core][io][storyboard]") {
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

    const auto folder = tempFolder("ppam_storyboard_test.ppproj");
    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
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

    std::filesystem::remove_all(folder);
}

TEST_CASE("Setting boards round trip through ppproj", "[core][io][settingboard]") {
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

    const auto folder = tempFolder("ppam_settingboard_test.ppproj");
    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
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

    std::filesystem::remove_all(folder);
}

TEST_CASE("Canvas size round trips through ppproj", "[core][io][canvassize]") {
    const auto folder = tempFolder("ppam_canvassize_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("CanvasSize");
    project.setCanvasSize(2048, 858);  // シネスコ(2.39:1)
    project.addScene("S").addCut("C").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->canvasWidth() == 2048);
    REQUIRE(loaded->canvasHeight() == 858);

    std::filesystem::remove_all(folder);
}

TEST_CASE("Canvas size defaults to Full HD when absent from an old project file", "[core][io][canvassize]") {
    const auto folder = tempFolder("ppam_canvassize_default_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("Default");
    project.addScene("S").addCut("C").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->canvasWidth() == 1920);
    REQUIRE(loaded->canvasHeight() == 1080);

    std::filesystem::remove_all(folder);
}

TEST_CASE("Canvas size setter clamps out-of-range values", "[core][canvassize]") {
    core::Project project("Clamp");
    project.setCanvasSize(4, 999999);
    REQUIRE(project.canvasWidth() == 16);
    REQUIRE(project.canvasHeight() == 8192);
}

TEST_CASE("Cut status round trips through ppproj", "[core][io][edit]") {
    core::Project project("P");
    core::Scene& scene = project.addScene("S");
    core::Cut& cutA = scene.addCut("Cut A");
    cutA.addCel("A").addLayer("L").addFrame();  // 最小構成
    cutA.setStatus(core::CutStatus::Finishing);

    core::Cut& cutB = scene.addCut("Cut B");  // 未設定(既定のNotStartedのまま)のカットも往復確認する
    cutB.addCel("A").addLayer("L").addFrame();

    const auto folder = tempFolder("ppam_cutstatus_test.ppproj");
    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));
    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);

    REQUIRE(loaded->scene(0).cut(0).status() == core::CutStatus::Finishing);
    REQUIRE(loaded->scene(0).cut(1).status() == core::CutStatus::NotStarted);

    std::filesystem::remove_all(folder);
}

// --- 新形式(プロジェクトフォルダ)特有の挙動 ---

TEST_CASE("ProjectIO save creates the expected folder layout", "[core][io][folder]") {
    const auto folder = tempFolder("ppam_layout_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("Layout");
    core::Scene& scene = project.addScene("S");
    scene.addCut("Cut A").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));

    REQUIRE(std::filesystem::exists(folder / "project.ppam"));
    REQUIRE(std::filesystem::exists(folder / "storyboard.ppam"));
    REQUIRE(std::filesystem::exists(folder / "boards.ppam"));
    REQUIRE(std::filesystem::exists(folder / "cuts"));
    REQUIRE(std::filesystem::exists(folder / "cuts" / "cut_1.ppam"));

    std::filesystem::remove_all(folder);
}

TEST_CASE("Cut IDs persist across save/load and orphan cut files are cleaned up", "[core][io][cutid]") {
    const auto folder = tempFolder("ppam_cutid_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("Ids");
    core::Scene& scene = project.addScene("S");
    scene.addCut("Cut A").addCel("A").addLayer("L").addFrame();
    scene.addCut("Cut B").addCel("A").addLayer("L").addFrame();
    scene.addCut("Cut C").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));

    // 採番されたIDが保存前後でわかるよう控えておく
    const uint64_t idA = project.scene(0).cut(0).id();
    const uint64_t idB = project.scene(0).cut(1).id();
    const uint64_t idC = project.scene(0).cut(2).id();
    REQUIRE(idA != 0);
    REQUIRE(idB != 0);
    REQUIRE(idC != 0);
    REQUIRE(idA != idB);
    REQUIRE(idB != idC);

    auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->scene(0).cut(0).id() == idA);
    REQUIRE(loaded->scene(0).cut(1).id() == idB);
    REQUIRE(loaded->scene(0).cut(2).id() == idC);

    // Cut Bを削除して再保存すると、そのcut_<id>.ppamが孤児として消える
    loaded->scene(0).removeCut(1);
    REQUIRE(core::ProjectIO::save(*loaded, folder, &error));

    REQUIRE_FALSE(std::filesystem::exists(folder / "cuts" / ("cut_" + std::to_string(idB) + ".ppam")));
    REQUIRE(std::filesystem::exists(folder / "cuts" / ("cut_" + std::to_string(idA) + ".ppam")));
    REQUIRE(std::filesystem::exists(folder / "cuts" / ("cut_" + std::to_string(idC) + ".ppam")));

    std::filesystem::remove_all(folder);
}

TEST_CASE("Partial save only rewrites the requested cut files", "[core][io][partial]") {
    const auto folder = tempFolder("ppam_partial_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("Partial");
    core::Scene& scene = project.addScene("S");
    core::Cut& cut1 = scene.addCut("Cut 1");
    cut1.addCel("A").addLayer("L").addFrame().bitmap() = core::Bitmap(4, 4);
    core::Cut& cut2 = scene.addCut("Cut 2");
    cut2.addCel("A").addLayer("L").addFrame().bitmap() = core::Bitmap(4, 4);

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));  // 全保存(ID採番も行われる)

    const uint64_t id1 = project.scene(0).cut(0).id();

    // カット1のピクセルを1個だけ変更する
    project.scene(0).cut(0).cel(0).layer(0).frame(0).bitmap().setPixel(0, 0, {9, 9, 9, 255});

    core::SaveOptions options;
    options.writeProject = false;
    options.writeStoryboard = false;
    options.writeBoards = false;
    options.writeAllCuts = false;
    options.cutIds = {id1};
    REQUIRE(core::ProjectIO::save(project, folder, &error, &options));

    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->sceneCount() == 1);
    REQUIRE(loaded->scene(0).cutCount() == 2);

    const auto changedPixel = loaded->scene(0).cut(0).cel(0).layer(0).frame(0).bitmap().pixel(0, 0);
    REQUIRE(changedPixel.r == 9);
    REQUIRE(changedPixel.g == 9);
    REQUIRE(changedPixel.b == 9);

    // カット2は変更していないので元のまま(空でない4x4の透明ビットマップ)
    REQUIRE(loaded->scene(0).cut(1).name() == "Cut 2");
    REQUIRE_FALSE(loaded->scene(0).cut(1).cel(0).layer(0).frame(0).bitmap().isEmpty());

    std::filesystem::remove_all(folder);
}

TEST_CASE("ProjectIO load accepts both the folder path and the project.ppam path", "[core][io][folder]") {
    const auto folder = tempFolder("ppam_loadpath_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("LoadPath");
    project.addScene("S").addCut("C").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));

    const auto loadedByFolder = core::ProjectIO::load(folder, &error);
    REQUIRE(loadedByFolder != nullptr);

    const auto loadedByFile = core::ProjectIO::load(folder / "project.ppam", &error);
    REQUIRE(loadedByFile != nullptr);
    REQUIRE(loadedByFile->name() == "LoadPath");

    std::filesystem::remove_all(folder);
}

TEST_CASE("ProjectIO load fails with a filename-specific error when a cut file is missing", "[core][io][error]") {
    const auto folder = tempFolder("ppam_missingcut_test.ppproj");
    std::filesystem::remove_all(folder);

    core::Project project("MissingCut");
    project.addScene("S").addCut("C").addCel("A").addLayer("L").addFrame();

    std::string error;
    REQUIRE(core::ProjectIO::save(project, folder, &error));

    const uint64_t id = project.scene(0).cut(0).id();
    std::filesystem::remove(folder / "cuts" / ("cut_" + std::to_string(id) + ".ppam"));

    const auto loaded = core::ProjectIO::load(folder, &error);
    REQUIRE(loaded == nullptr);
    REQUIRE_FALSE(error.empty());
    REQUIRE(error.find("cut_" + std::to_string(id) + ".ppam") != std::string::npos);

    std::filesystem::remove_all(folder);
}

TEST_CASE("ProjectIO load reports errors for invalid files", "[core][io]") {
    std::string error;

    SECTION("nonexistent folder") {
        REQUIRE(core::ProjectIO::load(tempFolder("ppam_missing_xyz.ppproj"), &error) == nullptr);
        REQUIRE_FALSE(error.empty());
    }

    SECTION("wrong magic") {
        const auto folder = tempFolder("ppam_badmagic.ppproj");
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        std::ofstream(folder / "project.ppam", std::ios::binary) << "NOPE0000000000000000";
        REQUIRE(core::ProjectIO::load(folder, &error) == nullptr);
        REQUIRE(error.find("プロジェクトファイルではありません") != std::string::npos);
        std::filesystem::remove_all(folder);
    }

    SECTION("future container version") {
        const auto folder = tempFolder("ppam_future.ppproj");
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        {
            std::ofstream out(folder / "project.ppam", std::ios::binary);
            out.write("PPAM", 4);
            const uint32_t version = 999;
            const uint64_t jsonSize = 0;
            out.write(reinterpret_cast<const char*>(&version), 4);
            out.write(reinterpret_cast<const char*>(&jsonSize), 8);
        }
        REQUIRE(core::ProjectIO::load(folder, &error) == nullptr);
        REQUIRE(error.find("新しいバージョン") != std::string::npos);
        std::filesystem::remove_all(folder);
    }

    SECTION("truncated project.ppam") {
        const auto folder = tempFolder("ppam_truncated.ppproj");
        std::filesystem::remove_all(folder);
        core::Project project = makeSampleProject();
        REQUIRE(core::ProjectIO::save(project, folder, &error));

        const auto projectPath = folder / "project.ppam";
        // project.ppam本体(ヘッダ+JSON)の末尾40%を切り落とす
        const auto fullSize = std::filesystem::file_size(projectPath);
        std::filesystem::resize_file(projectPath, fullSize * 6 / 10);
        REQUIRE(core::ProjectIO::load(folder, &error) == nullptr);
        REQUIRE_FALSE(error.empty());
        std::filesystem::remove_all(folder);
    }
}
