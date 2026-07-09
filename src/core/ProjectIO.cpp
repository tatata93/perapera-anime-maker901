#include "ProjectIO.h"

#include <zlib.h>

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
                    jLayers.push_back(
                        {{"name", layer.name()}, {"visible", layer.visible()}, {"frames", std::move(jFrames)}});
                }
                jCels.push_back({{"name", cel.name()}, {"visible", cel.visible()}, {"layers", std::move(jLayers)}});
            }
            jCuts.push_back({{"name", cut.name()}, {"cels", std::move(jCels)}});
        }
        jScenes.push_back({{"name", scene.name()}, {"cuts", std::move(jCuts)}});
    }

    json root;
    root["schemaVersion"] = kSchemaVersion;
    root["project"] = {{"name", project.name()}, {"scenes", std::move(jScenes)}};
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
            for (const json& jCut : jScene.at("cuts")) {
                Cut& cut = scene.addCut(jCut.at("name").get<std::string>());
                for (const json& jCel : jCut.at("cels")) {
                    Cel& cel = cut.addCel(jCel.at("name").get<std::string>());
                    cel.setVisible(jCel.value("visible", true));
                    for (const json& jLayer : jCel.at("layers")) {
                        Layer& layer = cel.addLayer(jLayer.at("name").get<std::string>());
                        if (!loadLayerFrames(layer, jLayer)) return nullptr;
                    }
                }
            }
        }
        return project;
    } catch (const std::exception& e) {
        setError(errorOut, std::string("ファイルの解析に失敗しました: ") + e.what());
        return nullptr;
    }
}

}  // namespace core
