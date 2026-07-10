#include "ProjectIO.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace core {

namespace {

using nlohmann::json;

constexpr char kMagic[4] = {'P', 'P', 'A', 'M'};
constexpr size_t kHeaderSize = 4 + 4 + 8;  // magic + containerVersion + jsonSize

void setError(std::string* errorOut, std::string message) {
    if (errorOut) *errorOut = std::move(message);
}

// LayerRole <-> 文字列の変換。不明値は呼び出し側でNormalとして扱う
std::string layerRoleToString(LayerRole role) {
    switch (role) {
        case LayerRole::ColorTrace:
            return "colorTrace";
        case LayerRole::Correction:
            return "correction";
        case LayerRole::Normal:
        default:
            return "normal";
    }
}

LayerRole layerRoleFromString(const std::string& value) {
    if (value == "colorTrace") return LayerRole::ColorTrace;
    if (value == "correction") return LayerRole::Correction;
    return LayerRole::Normal;  // 不明値・欠落はNormal扱い
}

// --- プリビズのJSON変換 ---

json vec3ToJson(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 vec3FromJson(const json& j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }

json transformToJson(const PrevizTransform& t) {
    return {{"position", vec3ToJson(t.position)}, {"rotation", vec3ToJson(t.rotationDeg)}, {"scale", vec3ToJson(t.scale)}};
}
PrevizTransform transformFromJson(const json& j) {
    return {vec3FromJson(j.at("position")), vec3FromJson(j.at("rotation")), vec3FromJson(j.at("scale"))};
}

json cameraStateToJson(const PrevizCameraState& s) {
    return {{"position", vec3ToJson(s.position)}, {"rotation", vec3ToJson(s.rotationDeg)}, {"focal", s.focalLengthMm}};
}
PrevizCameraState cameraStateFromJson(const json& j) {
    return {vec3FromJson(j.at("position")), vec3FromJson(j.at("rotation")), j.at("focal").get<float>()};
}

json previzToJson(const PrevizScene& scene) {
    json jModels = json::array();
    for (const PrevizModel& model : scene.models) {
        json jKeys = json::array();
        for (const auto& [frame, transform] : model.transformKeys) {
            jKeys.push_back({{"frame", frame}, {"transform", transformToJson(transform)}});
        }
        jModels.push_back({{"name", model.name},
                           {"filePath", model.filePath},
                           {"transform", transformToJson(model.transform)},
                           {"keys", std::move(jKeys)}});
    }
    json jCameraKeys = json::array();
    for (const auto& [frame, state] : scene.camera.keys) {
        jCameraKeys.push_back({{"frame", frame}, {"state", cameraStateToJson(state)}});
    }
    return {{"models", std::move(jModels)},
            {"camera",
             {{"state", cameraStateToJson(scene.camera.state)},
              {"sensorWidth", scene.camera.sensorWidthMm},
              {"keys", std::move(jCameraKeys)}}}};
}

void previzFromJson(const json& j, PrevizScene& scene) {
    for (const json& jModel : j.at("models")) {
        PrevizModel model;
        model.name = jModel.at("name").get<std::string>();
        model.filePath = jModel.at("filePath").get<std::string>();
        model.transform = transformFromJson(jModel.at("transform"));
        for (const json& jKey : jModel.at("keys")) {
            model.transformKeys[jKey.at("frame").get<size_t>()] = transformFromJson(jKey.at("transform"));
        }
        scene.models.push_back(std::move(model));
    }
    const json& jCamera = j.at("camera");
    scene.camera.state = cameraStateFromJson(jCamera.at("state"));
    scene.camera.sensorWidthMm = jCamera.at("sensorWidth").get<float>();
    for (const json& jKey : jCamera.at("keys")) {
        scene.camera.keys[jKey.at("frame").get<size_t>()] = cameraStateFromJson(jKey.at("state"));
    }
}

}  // namespace

bool ProjectIO::save(const Project& project, const std::filesystem::path& path, std::string* errorOut) {
    std::vector<unsigned char> blobs;

    json jScenes = json::array();
    for (size_t si = 0; si < project.sceneCount(); ++si) {
        const Scene& scene = project.scene(si);
        json jCuts = json::array();
        for (size_t ci = 0; ci < scene.cutCount(); ++ci) {
            const Cut& cut = scene.cut(ci);
            json jCels = json::array();
            for (size_t ceIdx = 0; ceIdx < cut.celCount(); ++ceIdx) {
                const Cel& cel = cut.cel(ceIdx);
                json jLayers = json::array();
                for (size_t li = 0; li < cel.layerCount(); ++li) {
                    const Layer& layer = cel.layer(li);
                    json jFrames = json::array();
                    for (size_t fi = 0; fi < layer.frameCount(); ++fi) {
                        const Bitmap& bitmap = layer.frame(fi).bitmap();
                        json jFrame;
                        jFrame["width"] = bitmap.width();
                        jFrame["height"] = bitmap.height();
                        if (!bitmap.isEmpty()) {
                            uLongf compSize = compressBound(static_cast<uLong>(bitmap.byteSize()));
                            std::vector<unsigned char> compressed(compSize);
                            if (compress2(compressed.data(), &compSize, bitmap.data(),
                                          static_cast<uLong>(bitmap.byteSize()), Z_DEFAULT_COMPRESSION) != Z_OK) {
                                setError(errorOut, "ピクセルデータの圧縮に失敗しました");
                                return false;
                            }
                            jFrame["blobOffset"] = blobs.size();
                            jFrame["blobSize"] = static_cast<uint64_t>(compSize);
                            jFrame["rawSize"] = static_cast<uint64_t>(bitmap.byteSize());
                            blobs.insert(blobs.end(), compressed.begin(), compressed.begin() + compSize);
                        }
                        jFrames.push_back(std::move(jFrame));
                    }
                    jLayers.push_back({{"name", layer.name()},
                                        {"visible", layer.visible()},
                                        {"role", layerRoleToString(layer.role())},
                                        {"frames", std::move(jFrames)}});
                }
                // 位置キー(タップ/ペグ移動): [コマ, x, y] の配列
                json jPositionKeys = json::array();
                for (const auto& [keyFrame, position] : cel.positionKeys()) {
                    jPositionKeys.push_back({keyFrame, position.x, position.y});
                }
                jCels.push_back({{"name", cel.name()},
                                 {"visible", cel.visible()},
                                 {"exposure", cel.exposures()},
                                 {"positionKeys", std::move(jPositionKeys)},
                                 {"layers", std::move(jLayers)}});
            }
            json jCut = {{"name", cut.name()},
                         {"frameCount", cut.frameCount()},
                         {"action", cut.action()},
                         {"dialogue", cut.dialogue()},
                         {"cels", std::move(jCels)}};
            if (!cut.previz().isEmpty()) jCut["previz"] = previzToJson(cut.previz());
            jCuts.push_back(std::move(jCut));
        }
        // 絵コンテ(パネル列)。絵はフレームと同じblob方式で圧縮する
        json jStoryboard = json::array();
        for (const StoryboardPanel& panel : scene.storyboard()) {
            json jPanel;
            jPanel["cutLabel"] = panel.cutLabel;
            jPanel["action"] = panel.action;
            jPanel["dialogue"] = panel.dialogue;
            jPanel["duration"] = panel.durationFrames;
            jPanel["width"] = panel.drawing.width();
            jPanel["height"] = panel.drawing.height();
            if (!panel.drawing.isEmpty()) {
                uLongf compSize = compressBound(static_cast<uLong>(panel.drawing.byteSize()));
                std::vector<unsigned char> compressed(compSize);
                if (compress2(compressed.data(), &compSize, panel.drawing.data(),
                              static_cast<uLong>(panel.drawing.byteSize()), Z_DEFAULT_COMPRESSION) != Z_OK) {
                    setError(errorOut, "ピクセルデータの圧縮に失敗しました");
                    return false;
                }
                jPanel["blobOffset"] = blobs.size();
                jPanel["blobSize"] = static_cast<uint64_t>(compSize);
                jPanel["rawSize"] = static_cast<uint64_t>(panel.drawing.byteSize());
                blobs.insert(blobs.end(), compressed.begin(), compressed.begin() + compSize);
            }
            jStoryboard.push_back(std::move(jPanel));
        }

        jScenes.push_back(
            {{"name", scene.name()}, {"storyboard", std::move(jStoryboard)}, {"cuts", std::move(jCuts)}});
    }

    json jProject = {{"name", project.name()}, {"scenes", std::move(jScenes)}};
    if (!project.palette().empty()) {
        // パレット色は[r,g,b,a]の配列として保存する(空なら省略)
        json jPalette = json::array();
        for (const Bitmap::Pixel& color : project.palette()) {
            jPalette.push_back({color.r, color.g, color.b, color.a});
        }
        jProject["palette"] = std::move(jPalette);
    }

    json root;
    root["schemaVersion"] = kSchemaVersion;
    root["project"] = std::move(jProject);
    const std::string jsonStr = root.dump();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setError(errorOut, "ファイルを開けませんでした(書き込み)");
        return false;
    }

    const uint32_t containerVersion = kContainerVersion;
    const uint64_t jsonSize = jsonStr.size();
    out.write(kMagic, sizeof(kMagic));
    out.write(reinterpret_cast<const char*>(&containerVersion), sizeof(containerVersion));
    out.write(reinterpret_cast<const char*>(&jsonSize), sizeof(jsonSize));
    out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    if (!blobs.empty()) {
        out.write(reinterpret_cast<const char*>(blobs.data()), static_cast<std::streamsize>(blobs.size()));
    }

    if (!out.good()) {
        setError(errorOut, "ファイルの書き込みに失敗しました");
        return false;
    }
    return true;
}

std::unique_ptr<Project> ProjectIO::load(const std::filesystem::path& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        setError(errorOut, "ファイルを開けませんでした(読み込み)");
        return nullptr;
    }
    const std::streamsize fileSize = in.tellg();
    in.seekg(0);
    if (fileSize < static_cast<std::streamsize>(kHeaderSize)) {
        setError(errorOut, "ファイルが壊れています(ヘッダが不足)");
        return nullptr;
    }

    std::vector<unsigned char> data(static_cast<size_t>(fileSize));
    if (!in.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        setError(errorOut, "ファイルの読み込みに失敗しました");
        return nullptr;
    }

    if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        setError(errorOut, "このアプリのプロジェクトファイルではありません");
        return nullptr;
    }

    uint32_t containerVersion = 0;
    uint64_t jsonSize = 0;
    std::memcpy(&containerVersion, data.data() + 4, sizeof(containerVersion));
    std::memcpy(&jsonSize, data.data() + 8, sizeof(jsonSize));

    if (containerVersion > kContainerVersion) {
        setError(errorOut, "より新しいバージョンのアプリで保存されたファイルです");
        return nullptr;
    }
    if (kHeaderSize + jsonSize > static_cast<uint64_t>(fileSize)) {
        setError(errorOut, "ファイルが壊れています(サイズ不整合)");
        return nullptr;
    }

    const unsigned char* blobBase = data.data() + kHeaderSize + jsonSize;
    const uint64_t blobTotal = static_cast<uint64_t>(fileSize) - kHeaderSize - jsonSize;

    try {
        const json root = json::parse(data.begin() + static_cast<ptrdiff_t>(kHeaderSize),
                                      data.begin() + static_cast<ptrdiff_t>(kHeaderSize + jsonSize));

        const int schemaVersion = root.at("schemaVersion").get<int>();
        if (schemaVersion > kSchemaVersion) {
            setError(errorOut, "より新しいバージョンのアプリで保存されたファイルです");
            return nullptr;
        }

        const json& jProject = root.at("project");
        auto project = std::make_unique<Project>(jProject.at("name").get<std::string>());

        // レイヤー1つ分(frames配列)を読み込む共通処理
        const auto loadLayerFrames = [&](Layer& layer, const json& jLayer) -> bool {
            layer.setVisible(jLayer.value("visible", true));
            layer.setRole(layerRoleFromString(jLayer.value("role", std::string("normal"))));
            for (const json& jFrame : jLayer.at("frames")) {
                Frame& frame = layer.addFrame();
                const int w = jFrame.at("width").get<int>();
                const int h = jFrame.at("height").get<int>();
                if (w <= 0 || h <= 0) continue;  // 空フレーム

                const uint64_t offset = jFrame.at("blobOffset").get<uint64_t>();
                const uint64_t blobSize = jFrame.at("blobSize").get<uint64_t>();
                const uint64_t rawSize = jFrame.at("rawSize").get<uint64_t>();

                Bitmap bitmap(w, h);
                if (rawSize != bitmap.byteSize() || offset + blobSize > blobTotal) {
                    setError(errorOut, "ファイルが壊れています(ピクセルデータ不整合)");
                    return false;
                }

                uLongf destLen = static_cast<uLongf>(rawSize);
                if (uncompress(bitmap.data(), &destLen, blobBase + offset, static_cast<uLong>(blobSize)) != Z_OK ||
                    destLen != rawSize) {
                    setError(errorOut, "ファイルが壊れています(展開に失敗)");
                    return false;
                }
                frame.bitmap() = std::move(bitmap);
            }
            return true;
        };

        for (const json& jScene : jProject.at("scenes")) {
            Scene& scene = project->addScene(jScene.at("name").get<std::string>());

            // 絵コンテ(任意)
            if (jScene.contains("storyboard")) {
                for (const json& jPanel : jScene.at("storyboard")) {
                    StoryboardPanel panel;
                    panel.cutLabel = jPanel.value("cutLabel", std::string());
                    panel.action = jPanel.value("action", std::string());
                    panel.dialogue = jPanel.value("dialogue", std::string());
                    panel.durationFrames = jPanel.value("duration", static_cast<size_t>(24));
                    const int w = jPanel.value("width", 0);
                    const int h = jPanel.value("height", 0);
                    if (w > 0 && h > 0 && jPanel.contains("blobOffset")) {
                        const uint64_t offset = jPanel.at("blobOffset").get<uint64_t>();
                        const uint64_t blobSize = jPanel.at("blobSize").get<uint64_t>();
                        const uint64_t rawSize = jPanel.at("rawSize").get<uint64_t>();
                        Bitmap bitmap(w, h);
                        if (rawSize != bitmap.byteSize() || offset + blobSize > blobTotal) {
                            setError(errorOut, "ファイルが壊れています(ピクセルデータ不整合)");
                            return nullptr;
                        }
                        uLongf destLen = static_cast<uLongf>(rawSize);
                        if (uncompress(bitmap.data(), &destLen, blobBase + offset, static_cast<uLong>(blobSize)) != Z_OK ||
                            destLen != rawSize) {
                            setError(errorOut, "ファイルが壊れています(展開に失敗)");
                            return nullptr;
                        }
                        panel.drawing = std::move(bitmap);
                    }
                    scene.storyboard().push_back(std::move(panel));
                }
            }
            for (const json& jCut : jScene.at("cuts")) {
                Cut& cut = scene.addCut(jCut.at("name").get<std::string>());
                for (const json& jCel : jCut.at("cels")) {
                    Cel& cel = cut.addCel(jCel.at("name").get<std::string>());
                    cel.setVisible(jCel.value("visible", true));
                    for (const json& jLayer : jCel.at("layers")) {
                        Layer& layer = cel.addLayer(jLayer.at("name").get<std::string>());
                        if (!loadLayerFrames(layer, jLayer)) return nullptr;
                    }
                    // 位置キー(タップ/ペグ移動)
                    if (jCel.contains("positionKeys")) {
                        for (const json& jKey : jCel.at("positionKeys")) {
                            cel.setPositionKey(jKey.at(0).get<size_t>(),
                                               {jKey.at(1).get<float>(), jKey.at(2).get<float>()});
                        }
                    }
                    // 露出表(欠落時は動画を1コマ打ちで並べた線形割付にする)
                    if (jCel.contains("exposure")) {
                        const auto exposure = jCel.at("exposure").get<std::vector<int>>();
                        for (size_t t = 0; t < exposure.size(); ++t) cel.setExposure(t, exposure[t]);
                    } else {
                        for (size_t t = 0; t < cel.drawingCount(); ++t) cel.setExposure(t, static_cast<int>(t));
                    }
                }
                // 絵コンテメモ(欠落時は空文字)
                cut.setAction(jCut.value("action", std::string()));
                cut.setDialogue(jCut.value("dialogue", std::string()));

                // プリビズシーン(任意)
                if (jCut.contains("previz")) previzFromJson(jCut.at("previz"), cut.previz());

                // 尺(欠落時は各セルの露出表/動画数から推定)
                size_t frameCount = jCut.value("frameCount", static_cast<size_t>(0));
                if (frameCount == 0) {
                    for (size_t ceIdx = 0; ceIdx < cut.celCount(); ++ceIdx) {
                        frameCount =
                            std::max({frameCount, cut.cel(ceIdx).exposures().size(), cut.cel(ceIdx).drawingCount()});
                    }
                }
                cut.setFrameCount(std::max<size_t>(1, frameCount));
            }
        }

        // パレットはオプショナル(存在しなければ空のまま)
        if (jProject.contains("palette")) {
            for (const json& jColor : jProject.at("palette")) {
                Bitmap::Pixel color;
                color.r = jColor.at(0).get<uint8_t>();
                color.g = jColor.at(1).get<uint8_t>();
                color.b = jColor.at(2).get<uint8_t>();
                color.a = jColor.at(3).get<uint8_t>();
                project->palette().push_back(color);
            }
        }
        return project;
    } catch (const std::exception& e) {
        setError(errorOut, std::string("ファイルの解析に失敗しました: ") + e.what());
        return nullptr;
    }
}

}  // namespace core
