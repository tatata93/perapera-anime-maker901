#include "ProjectIO.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace core {

namespace {

using nlohmann::json;

constexpr char kMagic[4] = {'P', 'P', 'A', 'M'};
constexpr size_t kHeaderSize = 4 + 4 + 8;  // magic + containerVersion + jsonSize

constexpr const char* kProjectFileName = "project.ppam";
constexpr const char* kStoryboardFileName = "storyboard.ppam";
constexpr const char* kBoardsFileName = "boards.ppam";
constexpr const char* kCutsDirName = "cuts";

void setError(std::string* errorOut, std::string message) {
    if (errorOut) *errorOut = std::move(message);
}

std::string cutFileName(uint64_t id) { return "cut_" + std::to_string(id) + ".ppam"; }

bool isHumanoidModelPath(const std::string& filePath) {
    return filePath == ":humanoid" || filePath == ":humanoid_box";
}

// "cut_<id>.ppam" からidを取り出す。形式に一致しなければfalse(孤児ファイル判定に使う)
bool parseCutId(const std::string& filename, uint64_t* idOut) {
    const std::string prefix = "cut_";
    const std::string suffix = ".ppam";
    if (filename.size() <= prefix.size() + suffix.size()) return false;
    if (filename.compare(0, prefix.size(), prefix) != 0) return false;
    if (filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    const std::string numPart = filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
    if (numPart.empty() ||
        !std::all_of(numPart.begin(), numPart.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    *idOut = std::stoull(numPart);
    return true;
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

json humanoidPoseToJson(const PrevizHumanoidPose& p) {
    return {{"torsoPitch", p.torsoPitchDeg},
            {"torsoRoll", p.torsoRollDeg},
            {"headPitch", p.headPitchDeg},
            {"headYaw", p.headYawDeg},
            {"leftShoulderPitch", p.leftShoulderPitchDeg},
            {"leftShoulderRoll", p.leftShoulderRollDeg},
            {"leftElbow", p.leftElbowDeg},
            {"rightShoulderPitch", p.rightShoulderPitchDeg},
            {"rightShoulderRoll", p.rightShoulderRollDeg},
            {"rightElbow", p.rightElbowDeg},
            {"leftHipPitch", p.leftHipPitchDeg},
            {"leftHipRoll", p.leftHipRollDeg},
            {"leftKnee", p.leftKneeDeg},
            {"rightHipPitch", p.rightHipPitchDeg},
            {"rightHipRoll", p.rightHipRollDeg},
            {"rightKnee", p.rightKneeDeg}};
}
PrevizHumanoidPose humanoidPoseFromJson(const json& j) {
    PrevizHumanoidPose p;
    p.torsoPitchDeg = j.value("torsoPitch", 0.0f);
    p.torsoRollDeg = j.value("torsoRoll", 0.0f);
    p.headPitchDeg = j.value("headPitch", 0.0f);
    p.headYawDeg = j.value("headYaw", 0.0f);
    p.leftShoulderPitchDeg = j.value("leftShoulderPitch", 0.0f);
    p.leftShoulderRollDeg = j.value("leftShoulderRoll", 0.0f);
    p.leftElbowDeg = j.value("leftElbow", 0.0f);
    p.rightShoulderPitchDeg = j.value("rightShoulderPitch", 0.0f);
    p.rightShoulderRollDeg = j.value("rightShoulderRoll", 0.0f);
    p.rightElbowDeg = j.value("rightElbow", 0.0f);
    p.leftHipPitchDeg = j.value("leftHipPitch", 0.0f);
    p.leftHipRollDeg = j.value("leftHipRoll", 0.0f);
    p.leftKneeDeg = j.value("leftKnee", 0.0f);
    p.rightHipPitchDeg = j.value("rightHipPitch", 0.0f);
    p.rightHipRollDeg = j.value("rightHipRoll", 0.0f);
    p.rightKneeDeg = j.value("rightKnee", 0.0f);
    return p;
}

json humanoidBodyToJson(const PrevizHumanoidBody& b) {
    return {{"headScale", b.headScale},
            {"headWidth", b.headWidth},
            {"headHeight", b.headHeight},
            {"headDepth", b.headDepth},
            {"faceWidth", b.faceWidth},
            {"faceHeight", b.faceHeight},
            {"faceDepth", b.faceDepth},
            {"torsoLength", b.torsoLength},
            {"chestHeight", b.chestHeight},
            {"bellyHeight", b.bellyHeight},
            {"waistHeight", b.waistHeight},
            {"chestWidth", b.chestWidth},
            {"bellyWidth", b.bellyWidth},
            {"waistWidth", b.waistWidth},
            {"chestDepth", b.chestDepth},
            {"bellyDepth", b.bellyDepth},
            {"waistDepth", b.waistDepth},
            {"shoulderWidth", b.shoulderWidth},
            {"hipWidth", b.hipWidth},
            {"armLength", b.armLength},
            {"armThickness", b.armThickness},
            {"armDepth", b.armDepth},
            {"legLength", b.legLength},
            {"legThickness", b.legThickness},
            {"legDepth", b.legDepth},
            {"handScale", b.handScale},
            {"handDepth", b.handDepth},
            {"footScale", b.footScale},
            {"footDepth", b.footDepth},
            {"leftArmLength", b.leftArmLength},
            {"rightArmLength", b.rightArmLength},
            {"leftArmThickness", b.leftArmThickness},
            {"rightArmThickness", b.rightArmThickness},
            {"leftLegLength", b.leftLegLength},
            {"rightLegLength", b.rightLegLength},
            {"leftLegThickness", b.leftLegThickness},
            {"rightLegThickness", b.rightLegThickness},
            {"leftHandScale", b.leftHandScale},
            {"rightHandScale", b.rightHandScale},
            {"leftFootScale", b.leftFootScale},
            {"rightFootScale", b.rightFootScale},
            {"leftUpperArmLength", b.leftUpperArmLength},
            {"rightUpperArmLength", b.rightUpperArmLength},
            {"leftForearmLength", b.leftForearmLength},
            {"rightForearmLength", b.rightForearmLength},
            {"leftUpperArmThickness", b.leftUpperArmThickness},
            {"rightUpperArmThickness", b.rightUpperArmThickness},
            {"leftForearmThickness", b.leftForearmThickness},
            {"rightForearmThickness", b.rightForearmThickness},
            {"leftUpperArmDepth", b.leftUpperArmDepth},
            {"rightUpperArmDepth", b.rightUpperArmDepth},
            {"leftForearmDepth", b.leftForearmDepth},
            {"rightForearmDepth", b.rightForearmDepth},
            {"leftThighLength", b.leftThighLength},
            {"rightThighLength", b.rightThighLength},
            {"leftShinLength", b.leftShinLength},
            {"rightShinLength", b.rightShinLength},
            {"leftThighThickness", b.leftThighThickness},
            {"rightThighThickness", b.rightThighThickness},
            {"leftShinThickness", b.leftShinThickness},
            {"rightShinThickness", b.rightShinThickness},
            {"leftThighDepth", b.leftThighDepth},
            {"rightThighDepth", b.rightThighDepth},
            {"leftShinDepth", b.leftShinDepth},
            {"rightShinDepth", b.rightShinDepth}};
}
PrevizHumanoidBody humanoidBodyFromJson(const json& j) {
    PrevizHumanoidBody b;
    b.headScale = j.value("headScale", 1.0f);
    b.headWidth = j.value("headWidth", 1.0f);
    b.headHeight = j.value("headHeight", 1.0f);
    b.headDepth = j.value("headDepth", 1.0f);
    b.faceWidth = j.value("faceWidth", 1.0f);
    b.faceHeight = j.value("faceHeight", 1.0f);
    b.faceDepth = j.value("faceDepth", 1.0f);
    b.torsoLength = j.value("torsoLength", 1.0f);
    b.chestHeight = j.value("chestHeight", 1.0f);
    b.bellyHeight = j.value("bellyHeight", 1.0f);
    b.waistHeight = j.value("waistHeight", 1.0f);
    b.chestWidth = j.value("chestWidth", 1.0f);
    b.bellyWidth = j.value("bellyWidth", 1.0f);
    b.waistWidth = j.value("waistWidth", 1.0f);
    b.chestDepth = j.value("chestDepth", 1.0f);
    b.bellyDepth = j.value("bellyDepth", 1.0f);
    b.waistDepth = j.value("waistDepth", 1.0f);
    b.shoulderWidth = j.value("shoulderWidth", 1.0f);
    b.hipWidth = j.value("hipWidth", 1.0f);
    b.armLength = j.value("armLength", 1.0f);
    b.armThickness = j.value("armThickness", 1.0f);
    b.armDepth = j.value("armDepth", 1.0f);
    b.legLength = j.value("legLength", 1.0f);
    b.legThickness = j.value("legThickness", 1.0f);
    b.legDepth = j.value("legDepth", 1.0f);
    b.handScale = j.value("handScale", 1.0f);
    b.handDepth = j.value("handDepth", 1.0f);
    b.footScale = j.value("footScale", 1.0f);
    b.footDepth = j.value("footDepth", 1.0f);
    b.leftArmLength = j.value("leftArmLength", 1.0f);
    b.rightArmLength = j.value("rightArmLength", 1.0f);
    b.leftArmThickness = j.value("leftArmThickness", 1.0f);
    b.rightArmThickness = j.value("rightArmThickness", 1.0f);
    b.leftLegLength = j.value("leftLegLength", 1.0f);
    b.rightLegLength = j.value("rightLegLength", 1.0f);
    b.leftLegThickness = j.value("leftLegThickness", 1.0f);
    b.rightLegThickness = j.value("rightLegThickness", 1.0f);
    b.leftHandScale = j.value("leftHandScale", 1.0f);
    b.rightHandScale = j.value("rightHandScale", 1.0f);
    b.leftFootScale = j.value("leftFootScale", 1.0f);
    b.rightFootScale = j.value("rightFootScale", 1.0f);
    b.leftUpperArmLength = j.value("leftUpperArmLength", 1.0f);
    b.rightUpperArmLength = j.value("rightUpperArmLength", 1.0f);
    b.leftForearmLength = j.value("leftForearmLength", 1.0f);
    b.rightForearmLength = j.value("rightForearmLength", 1.0f);
    b.leftUpperArmThickness = j.value("leftUpperArmThickness", 1.0f);
    b.rightUpperArmThickness = j.value("rightUpperArmThickness", 1.0f);
    b.leftForearmThickness = j.value("leftForearmThickness", 1.0f);
    b.rightForearmThickness = j.value("rightForearmThickness", 1.0f);
    b.leftUpperArmDepth = j.value("leftUpperArmDepth", 1.0f);
    b.rightUpperArmDepth = j.value("rightUpperArmDepth", 1.0f);
    b.leftForearmDepth = j.value("leftForearmDepth", 1.0f);
    b.rightForearmDepth = j.value("rightForearmDepth", 1.0f);
    b.leftThighLength = j.value("leftThighLength", 1.0f);
    b.rightThighLength = j.value("rightThighLength", 1.0f);
    b.leftShinLength = j.value("leftShinLength", 1.0f);
    b.rightShinLength = j.value("rightShinLength", 1.0f);
    b.leftThighThickness = j.value("leftThighThickness", 1.0f);
    b.rightThighThickness = j.value("rightThighThickness", 1.0f);
    b.leftShinThickness = j.value("leftShinThickness", 1.0f);
    b.rightShinThickness = j.value("rightShinThickness", 1.0f);
    b.leftThighDepth = j.value("leftThighDepth", 1.0f);
    b.rightThighDepth = j.value("rightThighDepth", 1.0f);
    b.leftShinDepth = j.value("leftShinDepth", 1.0f);
    b.rightShinDepth = j.value("rightShinDepth", 1.0f);
    return b;
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
        json jModel = {{"name", model.name},
                       {"filePath", model.filePath},
                       {"transform", transformToJson(model.transform)},
                       {"keys", std::move(jKeys)}};
        if (isHumanoidModelPath(model.filePath) || !model.poseKeys.empty()) {
            json jPoseKeys = json::array();
            for (const auto& [frame, pose] : model.poseKeys) {
                jPoseKeys.push_back({{"frame", frame}, {"pose", humanoidPoseToJson(pose)}});
            }
            jModel["humanoidPose"] = humanoidPoseToJson(model.humanoidPose);
            jModel["humanoidBody"] = humanoidBodyToJson(model.humanoidBody);
            jModel["poseKeys"] = std::move(jPoseKeys);
        }
        jModels.push_back(std::move(jModel));
    }
    json jCameraKeys = json::array();
    for (const auto& [frame, state] : scene.camera.keys) {
        jCameraKeys.push_back({{"frame", frame}, {"state", cameraStateToJson(state)}});
    }
    return {{"models", std::move(jModels)},
            {"camera",
             {{"state", cameraStateToJson(scene.camera.state)},
              {"sensorWidth", scene.camera.sensorWidthMm},
              {"lensDistortion", scene.camera.lensDistortion},
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
        if (jModel.contains("humanoidPose")) {
            model.humanoidPose = humanoidPoseFromJson(jModel.at("humanoidPose"));
        }
        if (jModel.contains("humanoidBody")) {
            model.humanoidBody = humanoidBodyFromJson(jModel.at("humanoidBody"));
        }
        if (jModel.contains("poseKeys")) {
            for (const json& jKey : jModel.at("poseKeys")) {
                model.poseKeys[jKey.at("frame").get<size_t>()] = humanoidPoseFromJson(jKey.at("pose"));
            }
        }
        scene.models.push_back(std::move(model));
    }
    const json& jCamera = j.at("camera");
    scene.camera.state = cameraStateFromJson(jCamera.at("state"));
    scene.camera.sensorWidthMm = jCamera.at("sensorWidth").get<float>();
    scene.camera.lensDistortion = jCamera.value("lensDistortion", 0.0f);  // 旧データは歪みなし
    for (const json& jKey : jCamera.at("keys")) {
        scene.camera.keys[jKey.at("frame").get<size_t>()] = cameraStateFromJson(jKey.at("state"));
    }
}

// --- PPAMコンテナ(共通の読み書き) ---
// "PPAM"マジック+containerVersion+JSONサイズ+JSON+blobセクション(zlib圧縮)の共通フォーマット。
// project/storyboard/boards/cutの全ファイルがこのコンテナを流用する

bool writeContainer(const std::filesystem::path& path, const json& root, const std::vector<unsigned char>& blobs,
                     std::string* errorOut) {
    const std::string jsonStr = root.dump();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setError(errorOut, "ファイルを開けませんでした(書き込み): " + path.filename().string());
        return false;
    }

    const uint32_t containerVersion = ProjectIO::kContainerVersion;
    const uint64_t jsonSize = jsonStr.size();
    out.write(kMagic, sizeof(kMagic));
    out.write(reinterpret_cast<const char*>(&containerVersion), sizeof(containerVersion));
    out.write(reinterpret_cast<const char*>(&jsonSize), sizeof(jsonSize));
    out.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    if (!blobs.empty()) {
        out.write(reinterpret_cast<const char*>(blobs.data()), static_cast<std::streamsize>(blobs.size()));
    }

    if (!out.good()) {
        setError(errorOut, "ファイルの書き込みに失敗しました: " + path.filename().string());
        return false;
    }
    return true;
}

// 読み込んだコンテナ1つ分。data はファイル全体のバイト列で、blob参照の土台として保持し続ける
struct Container {
    json root;
    std::vector<unsigned char> data;
    uint64_t jsonSize = 0;

    const unsigned char* blobBase() const { return data.data() + kHeaderSize + jsonSize; }
    uint64_t blobTotal() const { return static_cast<uint64_t>(data.size()) - kHeaderSize - jsonSize; }
};

bool readContainer(const std::filesystem::path& path, Container* out, std::string* errorOut) {
    const std::string display = path.filename().string();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        setError(errorOut, "ファイルを開けませんでした(読み込み): " + display);
        return false;
    }
    const std::streamsize fileSize = in.tellg();
    in.seekg(0);
    if (fileSize < static_cast<std::streamsize>(kHeaderSize)) {
        setError(errorOut, "ファイルが壊れています(ヘッダが不足): " + display);
        return false;
    }

    out->data.resize(static_cast<size_t>(fileSize));
    if (!in.read(reinterpret_cast<char*>(out->data.data()), fileSize)) {
        setError(errorOut, "ファイルの読み込みに失敗しました: " + display);
        return false;
    }

    if (std::memcmp(out->data.data(), kMagic, sizeof(kMagic)) != 0) {
        setError(errorOut, "このアプリのプロジェクトファイルではありません: " + display);
        return false;
    }

    uint32_t containerVersion = 0;
    std::memcpy(&containerVersion, out->data.data() + 4, sizeof(containerVersion));
    std::memcpy(&out->jsonSize, out->data.data() + 8, sizeof(out->jsonSize));

    if (containerVersion > ProjectIO::kContainerVersion) {
        setError(errorOut, "より新しいバージョンのアプリで保存されたファイルです: " + display);
        return false;
    }
    if (kHeaderSize + out->jsonSize > static_cast<uint64_t>(fileSize)) {
        setError(errorOut, "ファイルが壊れています(サイズ不整合): " + display);
        return false;
    }

    try {
        out->root = json::parse(out->data.begin() + static_cast<ptrdiff_t>(kHeaderSize),
                                out->data.begin() + static_cast<ptrdiff_t>(kHeaderSize + out->jsonSize));
    } catch (const std::exception& e) {
        setError(errorOut, "ファイルの解析に失敗しました: " + display + ": " + e.what());
        return false;
    }

    const int schemaVersion = out->root.value("schemaVersion", 0);
    if (schemaVersion > ProjectIO::kSchemaVersion) {
        setError(errorOut, "より新しいバージョンのアプリで保存されたファイルです: " + display);
        return false;
    }
    return true;
}

// ビットマップ1枚をblobsへ圧縮追記し、対応するJSONフィールド(width/height/blobOffset等)を書く。
// 空ビットマップならwidth/heightのみ書く(blob追記なし)
bool writeBitmapBlob(const Bitmap& bitmap, json& jOut, std::vector<unsigned char>& blobs, std::string* errorOut) {
    jOut["width"] = bitmap.width();
    jOut["height"] = bitmap.height();
    if (bitmap.isEmpty()) return true;

    uLongf compSize = compressBound(static_cast<uLong>(bitmap.byteSize()));
    std::vector<unsigned char> compressed(compSize);
    if (compress2(compressed.data(), &compSize, bitmap.data(), static_cast<uLong>(bitmap.byteSize()),
                  Z_DEFAULT_COMPRESSION) != Z_OK) {
        setError(errorOut, "ピクセルデータの圧縮に失敗しました");
        return false;
    }
    jOut["blobOffset"] = blobs.size();
    jOut["blobSize"] = static_cast<uint64_t>(compSize);
    jOut["rawSize"] = static_cast<uint64_t>(bitmap.byteSize());
    blobs.insert(blobs.end(), compressed.begin(), compressed.begin() + compSize);
    return true;
}

// writeBitmapBlobの逆。jSrcにwidth/heightが無い・0以下なら空のまま(*bitmapOutは未変更)
bool readBitmapBlob(const json& jSrc, const unsigned char* blobBase, uint64_t blobTotal, Bitmap* bitmapOut,
                     std::string* errorOut) {
    const int w = jSrc.value("width", 0);
    const int h = jSrc.value("height", 0);
    if (w <= 0 || h <= 0 || !jSrc.contains("blobOffset")) return true;  // 空ビットマップ

    const uint64_t offset = jSrc.at("blobOffset").get<uint64_t>();
    const uint64_t blobSize = jSrc.at("blobSize").get<uint64_t>();
    const uint64_t rawSize = jSrc.at("rawSize").get<uint64_t>();

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
    *bitmapOut = std::move(bitmap);
    return true;
}

bool writePaintLayers(const std::vector<PaintLayer>& layers, json& owner, std::vector<unsigned char>& blobs,
                      std::string* errorOut) {
    if (layers.empty()) return true;

    json jLayers = json::array();
    for (const PaintLayer& layer : layers) {
        json jLayer;
        jLayer["name"] = layer.name;
        jLayer["visible"] = layer.visible;
        jLayer["opacity"] = std::clamp(layer.opacity, 0.0, 1.0);
        jLayer["role"] = layerRoleToString(layer.role);
        if (!writeBitmapBlob(layer.bitmap, jLayer, blobs, errorOut)) return false;
        jLayers.push_back(std::move(jLayer));
    }
    owner["layers"] = std::move(jLayers);
    return true;
}

bool readPaintLayers(const json& owner, const unsigned char* blobBase, uint64_t blobTotal,
                     std::vector<PaintLayer>* layersOut, std::string* errorOut) {
    layersOut->clear();
    if (!owner.contains("layers") || !owner.at("layers").is_array()) return true;

    for (const json& jLayer : owner.at("layers")) {
        PaintLayer layer;
        layer.name = jLayer.value("name", std::string());
        layer.visible = jLayer.value("visible", true);
        layer.opacity = std::clamp(jLayer.value("opacity", 1.0), 0.0, 1.0);
        layer.role = layerRoleFromString(jLayer.value("role", std::string("normal")));
        if (!readBitmapBlob(jLayer, blobBase, blobTotal, &layer.bitmap, errorOut)) return false;
        layersOut->push_back(std::move(layer));
    }
    return true;
}

// --- cuts/cut_<id>.ppam 1個分のJSON変換 ---
// 現在jCutに入っている全項目(name/frameCount/action/dialogue/status/cels(exposure/positionKeys/
// paperW/H/layers/frames)/previz/cameraKeys/effects(params/paramCurves/mask)/
// multiplane(camera/planes/backlights))を漏れなく往復させる

bool buildCutJson(const Cut& cut, json* jCutOut, std::vector<unsigned char>* blobsOut, std::string* errorOut) {
    json jCels = json::array();
    for (size_t ceIdx = 0; ceIdx < cut.celCount(); ++ceIdx) {
        const Cel& cel = cut.cel(ceIdx);
        json jLayers = json::array();
        for (size_t li = 0; li < cel.layerCount(); ++li) {
            const Layer& layer = cel.layer(li);
            json jFrames = json::array();
            for (size_t fi = 0; fi < layer.frameCount(); ++fi) {
                json jFrame;
                if (!writeBitmapBlob(layer.frame(fi).bitmap(), jFrame, *blobsOut, errorOut)) return false;
                jFrames.push_back(std::move(jFrame));
            }
            jLayers.push_back({{"name", layer.name()},
                               {"visible", layer.visible()},
                               {"opacity", layer.opacity()},
                               {"role", layerRoleToString(layer.role())},
                               {"frames", std::move(jFrames)}});
        }
        // 位置キー(タップ/ペグ移動): [コマ, x, y] の配列
        json jPositionKeys = json::array();
        for (const auto& [keyFrame, position] : cel.positionKeys()) {
            jPositionKeys.push_back({keyFrame, position.x, position.y});
        }
        json jCelEntry = {{"name", cel.name()},
                          {"visible", cel.visible()},
                          {"opacity", cel.opacity()},
                          {"exposure", cel.exposures()},
                          {"positionKeys", std::move(jPositionKeys)},
                          {"layers", std::move(jLayers)}};
        // 用紙サイズ(引きセル)。0(キャンバスサイズに従う既定)は省略する
        if (cel.paperWidth() > 0) jCelEntry["paperWidth"] = cel.paperWidth();
        if (cel.paperHeight() > 0) jCelEntry["paperHeight"] = cel.paperHeight();
        jCels.push_back(std::move(jCelEntry));
    }

    json jCut = {{"fileType", "cut"},
                {"schemaVersion", ProjectIO::kSchemaVersion},
                {"id", cut.id()},
                {"name", cut.name()},
                {"frameCount", cut.frameCount()},
                {"action", cut.action()},
                {"dialogue", cut.dialogue()},
                {"status", static_cast<int>(cut.status())},
                {"cels", std::move(jCels)}};

    if (!cut.previz().isEmpty()) jCut["previz"] = previzToJson(cut.previz());

    // カメラフレーム(画面に写る範囲)のキー。キー無しなら省略する
    if (!cut.cameraKeys().empty()) {
        json jCameraKeys = json::array();
        for (const auto& [keyFrame, camState] : cut.cameraKeys()) {
            jCameraKeys.push_back({{"frame", keyFrame},
                                   {"cx", camState.center.x},
                                   {"cy", camState.center.y},
                                   {"scale", camState.scale}});
        }
        jCut["cameraKeys"] = std::move(jCameraKeys);
    }

    // 撮影エフェクトのスタック。空なら省略する
    if (!cut.effects().empty()) {
        json jEffects = json::array();
        for (const Effect& effect : cut.effects()) {
            json jParams = json::object();
            for (const auto& [key, value] : effect.params) jParams[key] = value;
            json jEffect = {{"type", static_cast<int>(effect.type)},
                            {"enabled", effect.enabled},
                            {"targetCel", effect.targetCel},
                            {"params", std::move(jParams)}};
            // 適用範囲のコマ(in/out点)。既定値(0/-1)なら省略する
            if (effect.startFrame != 0) jEffect["startFrame"] = effect.startFrame;
            if (effect.endFrame != -1) jEffect["endFrame"] = effect.endFrame;
            // パラメータ単位のキーフレーム曲線(パラメータ名→[{コマ,値}])。空なら省略する
            if (!effect.paramCurves.empty()) {
                json jCurves = json::object();
                for (const auto& [key, curve] : effect.paramCurves) {
                    if (curve.empty()) continue;
                    json jKeys = json::array();
                    for (const auto& [keyFrame, value] : curve) {
                        jKeys.push_back({{"frame", keyFrame}, {"value", value}});
                    }
                    jCurves[key] = std::move(jKeys);
                }
                if (!jCurves.empty()) jEffect["paramCurves"] = std::move(jCurves);
            }
            // 適用範囲マスク(画面座標のグレースケール)。空なら省略
            if (!effect.mask.isEmpty()) {
                json jMask;
                if (!writeBitmapBlob(effect.mask, jMask, *blobsOut, errorOut)) return false;
                jEffect["mask"] = std::move(jMask);
            }
            jEffects.push_back(std::move(jEffect));
        }
        jCut["effects"] = std::move(jEffects);
    }

    // クラシック撮影(マルチプレーン撮影台)設定。無効かつ段が空なら省略する
    if (cut.multiplane().enabled || !cut.multiplane().planes.empty()) {
        const MultiplaneSetup& mp = cut.multiplane();
        json jPlanes = json::array();
        for (const MultiplaneCelPlane& p : mp.planes) {
            json jPlane = {{"cel", p.celIndex}, {"distance", p.distanceMm}, {"width", p.widthMm}};
            // 距離ブラシ(セル内の色塗り分け)。色マップとスロット(色+距離)を保存。空なら省略
            if (!p.distanceMap.isEmpty() && !p.distanceStops.empty()) {
                json jDist;
                if (!writeBitmapBlob(p.distanceMap, jDist, *blobsOut, errorOut)) return false;
                jPlane["distanceMap"] = std::move(jDist);
                json jStops = json::array();
                for (const MultiplaneDistanceStop& stop : p.distanceStops) {
                    jStops.push_back({{"distance", stop.distanceMm}, {"r", stop.r}, {"g", stop.g}, {"b", stop.b}});
                }
                jPlane["distanceStops"] = std::move(jStops);
            }
            jPlanes.push_back(std::move(jPlane));
        }
        json jMultiplane = {{"enabled", mp.enabled},
                            {"camera",
                             {{"focal", mp.camera.focalLengthMm},
                              {"sensor", mp.camera.sensorWidthMm},
                              {"fstop", mp.camera.apertureFStop},
                              {"focus", mp.camera.focusDistanceMm}}},
                            {"samples", mp.samplesPerPixel},
                            {"exportSamples", mp.exportSamplesPerPixel},
                            {"planes", std::move(jPlanes)},
                            {"framingLock", mp.framingLock},
                            {"framingWidth", mp.framingWidthMm},
                            {"framingRefDistance", mp.framingRefDistanceMm}};

        // コマ→値のキーフレーム曲線。空なら省略する
        const auto writeKeyCurve = [](const std::map<size_t, double>& keys) {
            json arr = json::array();
            for (const auto& [keyFrame, value] : keys) arr.push_back({{"frame", keyFrame}, {"value", value}});
            return arr;
        };

        // 透過光(T光)。複数灯対応: 灯ごとに名前/色/強度/塗料透過率/にじみ/マスク/点滅キーを持つ。
        // 空なら省略する
        if (!mp.backlights.empty()) {
            json jBacklights = json::array();
            for (const MultiplaneBacklight& bl : mp.backlights) {
                json jBl = {{"name", bl.name},
                           {"enabled", bl.enabled},
                           {"intensity", bl.intensity},
                           {"r", bl.colorR},
                           {"g", bl.colorG},
                           {"b", bl.colorB},
                           {"tau", bl.paintTransmittance},
                           {"bloomRadius", bl.bloomRadiusPx},
                           {"bloomStrength", bl.bloomStrength}};
                // 光源マスク(ペンで塗った範囲)。空なら省略する
                if (!bl.mask.isEmpty()) {
                    json jMask;
                    if (!writeBitmapBlob(bl.mask, jMask, *blobsOut, errorOut)) return false;
                    jBl["mask"] = std::move(jMask);
                }
                // セル/レイヤーを光源マスクとして使う指定(既定-1=なしなら省略)
                if (bl.maskCelIndex >= 0) jBl["maskCel"] = bl.maskCelIndex;
                if (bl.maskLayerIndex >= 0) jBl["maskLayer"] = bl.maskLayerIndex;
                // この灯だけの点滅キー(空なら省略)
                if (!bl.intensityKeys.empty()) jBl["intensityKeys"] = writeKeyCurve(bl.intensityKeys);
                jBacklights.push_back(std::move(jBl));
            }
            jMultiplane["backlights"] = std::move(jBacklights);
        }

        // カメラのコマキー(滑らかな変化=focal/focus/fstop)。空なら省略する
        if (!mp.focalKeys.empty()) jMultiplane["focalKeys"] = writeKeyCurve(mp.focalKeys);
        if (!mp.focusKeys.empty()) jMultiplane["focusKeys"] = writeKeyCurve(mp.focusKeys);
        if (!mp.fstopKeys.empty()) jMultiplane["fstopKeys"] = writeKeyCurve(mp.fstopKeys);

        jCut["multiplane"] = std::move(jMultiplane);
    }

    *jCutOut = std::move(jCut);
    return true;
}

bool writeCutFile(const Cut& cut, const std::filesystem::path& path, std::string* errorOut) {
    json jCut;
    std::vector<unsigned char> blobs;
    if (!buildCutJson(cut, &jCut, &blobs, errorOut)) return false;
    return writeContainer(path, jCut, blobs, errorOut);
}

// レイヤー1つ分(frames配列)を読み込む共通処理
bool loadLayerFrames(Layer& layer, const json& jLayer, const unsigned char* blobBase, uint64_t blobTotal,
                     std::string* errorOut) {
    layer.setVisible(jLayer.value("visible", true));
    layer.setOpacity(jLayer.value("opacity", 1.0));
    layer.setRole(layerRoleFromString(jLayer.value("role", std::string("normal"))));
    for (const json& jFrame : jLayer.at("frames")) {
        Frame& frame = layer.addFrame();
        Bitmap bitmap;
        if (!readBitmapBlob(jFrame, blobBase, blobTotal, &bitmap, errorOut)) return false;
        frame.bitmap() = std::move(bitmap);
    }
    return true;
}

// カット1個分(jCut)をcut(既にaddCutで作成済み)へ流し込む
bool parseCutJson(const json& jCut, Cut& cut, const unsigned char* blobBase, uint64_t blobTotal,
                  std::string* errorOut) {
    for (const json& jCel : jCut.at("cels")) {
        Cel& cel = cut.addCel(jCel.at("name").get<std::string>());
        cel.setVisible(jCel.value("visible", true));
        cel.setOpacity(jCel.value("opacity", 1.0));
        // 用紙サイズ(引きセル)。欠落時は0(キャンバスサイズに従う既定)のまま
        cel.setPaperSize(jCel.value("paperWidth", 0), jCel.value("paperHeight", 0));
        for (const json& jLayer : jCel.at("layers")) {
            Layer& layer = cel.addLayer(jLayer.at("name").get<std::string>());
            if (!loadLayerFrames(layer, jLayer, blobBase, blobTotal, errorOut)) return false;
        }
        // 位置キー(タップ/ペグ移動)
        if (jCel.contains("positionKeys")) {
            for (const json& jKey : jCel.at("positionKeys")) {
                cel.setPositionKey(jKey.at(0).get<size_t>(), {jKey.at(1).get<float>(), jKey.at(2).get<float>()});
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
    // 制作進捗(欠落時は未着手)
    cut.setStatus(static_cast<CutStatus>(jCut.value("status", 0)));

    // プリビズシーン(任意)
    if (jCut.contains("previz")) previzFromJson(jCut.at("previz"), cut.previz());

    // カメラフレーム(画面に写る範囲)のキー(任意、欠落時は空のまま)
    if (jCut.contains("cameraKeys")) {
        for (const json& jKey : jCut.at("cameraKeys")) {
            CameraFrameState state;
            state.center = {jKey.at("cx").get<float>(), jKey.at("cy").get<float>()};
            state.scale = jKey.at("scale").get<double>();
            cut.setCameraKey(jKey.at("frame").get<size_t>(), state);
        }
    }

    // 撮影エフェクトのスタック(任意、欠落時は空のまま)
    if (jCut.contains("effects")) {
        for (const json& jEffect : jCut.at("effects")) {
            Effect effect;
            effect.type = static_cast<EffectType>(jEffect.value("type", 0));
            effect.enabled = jEffect.value("enabled", true);
            effect.targetCel = jEffect.value("targetCel", -1);
            effect.startFrame = jEffect.value("startFrame", 0);
            effect.endFrame = jEffect.value("endFrame", -1);
            if (jEffect.contains("params")) {
                for (auto it = jEffect.at("params").begin(); it != jEffect.at("params").end(); ++it) {
                    effect.params[it.key()] = it.value().get<double>();
                }
            }
            // パラメータ単位のキーフレーム曲線(任意、欠落時は空のまま)
            if (jEffect.contains("paramCurves")) {
                const json& jCurves = jEffect.at("paramCurves");
                for (auto it = jCurves.begin(); it != jCurves.end(); ++it) {
                    for (const json& jKey : it.value()) {
                        effect.paramCurves[it.key()][jKey.at("frame").get<size_t>()] = jKey.at("value").get<double>();
                    }
                }
            }
            // 適用範囲マスク(任意、欠落時は空のまま)
            if (jEffect.contains("mask")) {
                if (!readBitmapBlob(jEffect.at("mask"), blobBase, blobTotal, &effect.mask, errorOut)) return false;
            }
            cut.effects().push_back(std::move(effect));
        }
    }

    // クラシック撮影(マルチプレーン撮影台)設定(任意、欠落時は既定=無効のまま)
    if (jCut.contains("multiplane")) {
        const json& jMp = jCut.at("multiplane");
        MultiplaneSetup& mp = cut.multiplane();
        mp.enabled = jMp.value("enabled", false);
        if (jMp.contains("camera")) {
            const json& jCam = jMp.at("camera");
            mp.camera.focalLengthMm = jCam.value("focal", 50.0);
            mp.camera.sensorWidthMm = jCam.value("sensor", 36.0);
            mp.camera.apertureFStop = jCam.value("fstop", 0.0);
            mp.camera.focusDistanceMm = jCam.value("focus", 500.0);
        }
        mp.samplesPerPixel = jMp.value("samples", 8);
        mp.exportSamplesPerPixel = jMp.value("exportSamples", 64);
        mp.framingLock = jMp.value("framingLock", true);
        mp.framingWidthMm = jMp.value("framingWidth", 360.0);
        mp.framingRefDistanceMm = jMp.value("framingRefDistance", 500.0);

        // コマ→値のキーフレーム曲線(任意、欠落時は空のまま)
        const auto readKeyCurve = [&jMp](const char* name, std::map<size_t, double>* out) {
            if (!jMp.contains(name)) return;
            for (const json& jKey : jMp.at(name)) {
                (*out)[jKey.at("frame").get<size_t>()] = jKey.at("value").get<double>();
            }
        };

        // 透過光(T光)。複数灯対応(任意、欠落時は空のまま=灯なし)
        if (jMp.contains("backlights")) {
            for (const json& jBl : jMp.at("backlights")) {
                MultiplaneBacklight bl;
                bl.name = jBl.value("name", std::string());
                bl.enabled = jBl.value("enabled", false);
                bl.intensity = jBl.value("intensity", 4.0);
                bl.colorR = jBl.value("r", 1.0);
                bl.colorG = jBl.value("g", 0.92);
                bl.colorB = jBl.value("b", 0.78);
                bl.paintTransmittance = jBl.value("tau", 0.1);
                bl.bloomRadiusPx = jBl.value("bloomRadius", 24.0);
                bl.bloomStrength = jBl.value("bloomStrength", 0.5);
                // 光源マスク(ペンで塗った範囲)。欠落時は空のまま
                if (jBl.contains("mask")) {
                    if (!readBitmapBlob(jBl.at("mask"), blobBase, blobTotal, &bl.mask, errorOut)) return false;
                }
                bl.maskCelIndex = jBl.value("maskCel", -1);
                bl.maskLayerIndex = jBl.value("maskLayer", -1);
                // この灯だけの点滅キー(任意、欠落時は空のまま)
                if (jBl.contains("intensityKeys")) {
                    for (const json& jKey : jBl.at("intensityKeys")) {
                        bl.intensityKeys[jKey.at("frame").get<size_t>()] = jKey.at("value").get<double>();
                    }
                }
                mp.backlights.push_back(std::move(bl));
            }
        }

        readKeyCurve("focalKeys", &mp.focalKeys);
        readKeyCurve("focusKeys", &mp.focusKeys);
        readKeyCurve("fstopKeys", &mp.fstopKeys);
        if (jMp.contains("planes")) {
            for (const json& jPlane : jMp.at("planes")) {
                MultiplaneCelPlane plane;
                plane.celIndex = jPlane.value("cel", 0);
                plane.distanceMm = jPlane.value("distance", 500.0);
                plane.widthMm = jPlane.value("width", 400.0);
                if (jPlane.contains("distanceStops")) {
                    for (const json& jStop : jPlane.at("distanceStops")) {
                        MultiplaneDistanceStop stop;
                        stop.distanceMm = jStop.value("distance", 500.0);
                        stop.r = static_cast<uint8_t>(jStop.value("r", 255));
                        stop.g = static_cast<uint8_t>(jStop.value("g", 0));
                        stop.b = static_cast<uint8_t>(jStop.value("b", 0));
                        plane.distanceStops.push_back(stop);
                    }
                }
                if (jPlane.contains("distanceMap")) {
                    if (!readBitmapBlob(jPlane.at("distanceMap"), blobBase, blobTotal, &plane.distanceMap, errorOut))
                        return false;
                }
                mp.planes.push_back(std::move(plane));
            }
        }
    }

    // 尺(欠落時は各セルの露出表/動画数から推定)
    size_t frameCount = jCut.value("frameCount", static_cast<size_t>(0));
    if (frameCount == 0) {
        for (size_t ceIdx = 0; ceIdx < cut.celCount(); ++ceIdx) {
            frameCount = std::max({frameCount, cut.cel(ceIdx).exposures().size(), cut.cel(ceIdx).drawingCount()});
        }
    }
    cut.setFrameCount(std::max<size_t>(1, frameCount));

    return true;
}

}  // namespace

bool ProjectIO::save(const Project& project, const std::filesystem::path& folder, std::string* errorOut,
                     const SaveOptions* options) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path cutsDir = folder / kCutsDirName;
    std::filesystem::create_directories(cutsDir, ec);
    if (!std::filesystem::is_directory(folder) || !std::filesystem::is_directory(cutsDir)) {
        setError(errorOut, "プロジェクトフォルダを作成できませんでした");
        return false;
    }

    const std::filesystem::path projectPath = folder / kProjectFileName;
    const bool isNewSave = !std::filesystem::exists(projectPath);

    // フォルダが新規保存先の場合はoptionsを無視して全書きする
    SaveOptions effective;
    if (options && !isNewSave) effective = *options;

    // カットの永続ID採番: id==0のカットへproject.nextCutIdから順に採番する(採番後カウンタ更新)。
    // これは公開APIの契約上constだが、保存時の遅延ID割当のために内部で書き換える
    Project& mutableProject = const_cast<Project&>(project);
    uint64_t nextId = mutableProject.nextCutId();
    std::vector<Cut*> allCuts;  // シーン順・カット順
    std::set<uint64_t> allCutIds;
    for (size_t si = 0; si < project.sceneCount(); ++si) {
        Scene& scene = const_cast<Scene&>(project.scene(si));
        for (size_t ci = 0; ci < scene.cutCount(); ++ci) {
            Cut& cut = scene.cut(ci);
            if (cut.id() == 0) cut.setId(nextId++);
            allCuts.push_back(&cut);
            allCutIds.insert(cut.id());
        }
    }
    mutableProject.setNextCutId(nextId);

    if (effective.writeProject) {
        json jScenes = json::array();
        for (size_t si = 0; si < project.sceneCount(); ++si) {
            const Scene& scene = project.scene(si);
            json jCutIds = json::array();
            for (size_t ci = 0; ci < scene.cutCount(); ++ci) jCutIds.push_back(scene.cut(ci).id());
            jScenes.push_back({{"name", scene.name()}, {"cutIds", std::move(jCutIds)}});
        }

        json root;
        root["fileType"] = "project";
        root["schemaVersion"] = kSchemaVersion;
        root["name"] = project.name();
        root["nextCutId"] = project.nextCutId();
        root["canvasWidth"] = project.canvasWidth();
        root["canvasHeight"] = project.canvasHeight();
        root["scenes"] = std::move(jScenes);
        if (!project.palette().empty()) {
            // パレット色は[r,g,b,a]の配列として保存する(空なら省略)
            json jPalette = json::array();
            for (const Bitmap::Pixel& color : project.palette()) {
                jPalette.push_back({color.r, color.g, color.b, color.a});
            }
            root["palette"] = std::move(jPalette);
        }

        if (!writeContainer(projectPath, root, {}, errorOut)) return false;
    }

    if (effective.writeStoryboard) {
        std::vector<unsigned char> blobs;
        json jScenes = json::array();
        for (size_t si = 0; si < project.sceneCount(); ++si) {
            const Scene& scene = project.scene(si);
            json jPanels = json::array();
            for (const StoryboardPanel& panel : scene.storyboard()) {
                json jPanel;
                jPanel["cutLabel"] = panel.cutLabel;
                jPanel["action"] = panel.action;
                jPanel["dialogue"] = panel.dialogue;
                jPanel["duration"] = panel.durationFrames;
                jPanel["activeLayer"] = panel.activeLayer;
                if (!writeBitmapBlob(panel.drawing, jPanel, blobs, errorOut)) return false;
                if (!writePaintLayers(panel.layers, jPanel, blobs, errorOut)) return false;
                jPanels.push_back(std::move(jPanel));
            }
            jScenes.push_back({{"panels", std::move(jPanels)}});
        }

        json root;
        root["fileType"] = "storyboard";
        root["schemaVersion"] = kSchemaVersion;
        root["scenes"] = std::move(jScenes);
        if (!writeContainer(folder / kStoryboardFileName, root, blobs, errorOut)) return false;
    }

    if (effective.writeBoards) {
        std::vector<unsigned char> blobs;
        json jBoards = json::array();
        for (const SettingBoard& board : project.settingBoards()) {
            json jBoard;
            jBoard["name"] = board.name;
            jBoard["finalStamp"] = board.finalStamp;
            jBoard["activeLayer"] = board.activeLayer;
            if (!writeBitmapBlob(board.image, jBoard, blobs, errorOut)) return false;
            if (!writePaintLayers(board.layers, jBoard, blobs, errorOut)) return false;
            // 色指定(名前付き色見本)。空なら省略する
            if (!board.colorSpecs.empty()) {
                json jColorSpecs = json::array();
                for (const ColorSpec& spec : board.colorSpecs) {
                    jColorSpecs.push_back({{"name", spec.name},
                                           {"r", spec.color.r},
                                           {"g", spec.color.g},
                                           {"b", spec.color.b},
                                           {"a", spec.color.a}});
                }
                jBoard["colorSpecs"] = std::move(jColorSpecs);
            }
            if (!board.textBoxes.empty()) {
                json jTextBoxes = json::array();
                for (const SettingBoardTextBox& box : board.textBoxes) {
                    jTextBoxes.push_back({{"text", box.text},
                                          {"x", box.x},
                                          {"y", box.y},
                                          {"width", box.width},
                                          {"height", box.height},
                                          {"fontPixelSize", box.fontPixelSize},
                                          {"r", box.color.r},
                                          {"g", box.color.g},
                                          {"b", box.color.b},
                                          {"a", box.color.a}});
                }
                jBoard["textBoxes"] = std::move(jTextBoxes);
            }
            jBoards.push_back(std::move(jBoard));
        }

        json root;
        root["fileType"] = "boards";
        root["schemaVersion"] = kSchemaVersion;
        root["boards"] = std::move(jBoards);
        if (!writeContainer(folder / kBoardsFileName, root, blobs, errorOut)) return false;
    }

    for (Cut* cutPtr : allCuts) {
        const bool shouldWrite = effective.writeAllCuts || effective.cutIds.count(cutPtr->id()) > 0;
        if (!shouldWrite) continue;
        if (!writeCutFile(*cutPtr, cutsDir / cutFileName(cutPtr->id()), errorOut)) return false;
    }

    if (effective.writeProject) {
        // 孤児ファイル掃除: cuts/内のcut_*.ppamのうち現在のカットID集合に無いものを削除する
        std::error_code dirEc;
        for (const auto& entry : std::filesystem::directory_iterator(cutsDir, dirEc)) {
            if (!entry.is_regular_file()) continue;
            uint64_t fileId = 0;
            if (parseCutId(entry.path().filename().string(), &fileId) && !allCutIds.count(fileId)) {
                std::filesystem::remove(entry.path(), dirEc);
            }
        }
    }

    return true;
}

std::unique_ptr<Project> ProjectIO::load(const std::filesystem::path& path, std::string* errorOut) {
    // pathは.ppprojフォルダ、またはその中のproject.ppamのどちらでも受け付ける
    const std::filesystem::path folder = (path.filename() == kProjectFileName) ? path.parent_path() : path;
    const std::filesystem::path projectPath = folder / kProjectFileName;

    Container projectContainer;
    if (!readContainer(projectPath, &projectContainer, errorOut)) return nullptr;
    if (projectContainer.root.value("fileType", std::string()) != "project") {
        setError(errorOut, "プロジェクトファイルではありません: " + std::string(kProjectFileName));
        return nullptr;
    }

    try {
        const json& jProject = projectContainer.root;
        auto project = std::make_unique<Project>(jProject.at("name").get<std::string>());
        project->setNextCutId(jProject.value("nextCutId", static_cast<uint64_t>(1)));
        // キャンバス解像度(欠落時=旧ファイルはフルHDの既定のまま)
        project->setCanvasSize(jProject.value("canvasWidth", 1920), jProject.value("canvasHeight", 1080));

        for (const json& jScene : jProject.at("scenes")) {
            Scene& scene = project->addScene(jScene.at("name").get<std::string>());
            if (!jScene.contains("cutIds")) continue;
            for (const json& jId : jScene.at("cutIds")) {
                const uint64_t cutId = jId.get<uint64_t>();
                const std::filesystem::path cutPath = folder / kCutsDirName / cutFileName(cutId);

                Container cutContainer;
                if (!readContainer(cutPath, &cutContainer, errorOut)) return nullptr;
                if (cutContainer.root.value("fileType", std::string()) != "cut") {
                    setError(errorOut, "カットファイルの種類が不正です: " + cutPath.filename().string());
                    return nullptr;
                }

                Cut& cut = scene.addCut(cutContainer.root.at("name").get<std::string>());
                cut.setId(cutId);
                if (!parseCutJson(cutContainer.root, cut, cutContainer.blobBase(), cutContainer.blobTotal(),
                                  errorOut)) {
                    return nullptr;
                }
            }
        }

        // 絵コンテ(storyboard.ppam)。無ければ空のまま許容する
        const std::filesystem::path storyboardPath = folder / kStoryboardFileName;
        if (std::filesystem::exists(storyboardPath)) {
            Container sbContainer;
            if (!readContainer(storyboardPath, &sbContainer, errorOut)) return nullptr;
            if (sbContainer.root.value("fileType", std::string()) == "storyboard" && sbContainer.root.contains("scenes")) {
                const json& jScenes = sbContainer.root.at("scenes");
                for (size_t si = 0; si < jScenes.size() && si < project->sceneCount(); ++si) {
                    const json& jScene = jScenes.at(si);
                    if (!jScene.contains("panels")) continue;
                    Scene& scene = project->scene(si);
                    for (const json& jPanel : jScene.at("panels")) {
                        StoryboardPanel panel;
                        panel.cutLabel = jPanel.value("cutLabel", std::string());
                        panel.action = jPanel.value("action", std::string());
                        panel.dialogue = jPanel.value("dialogue", std::string());
                        panel.durationFrames = jPanel.value("duration", static_cast<size_t>(24));
                        if (!readBitmapBlob(jPanel, sbContainer.blobBase(), sbContainer.blobTotal(), &panel.drawing,
                                            errorOut)) {
                            return nullptr;
                        }
                        panel.activeLayer = jPanel.value("activeLayer", static_cast<size_t>(0));
                        if (!readPaintLayers(jPanel, sbContainer.blobBase(), sbContainer.blobTotal(), &panel.layers,
                                             errorOut)) {
                            return nullptr;
                        }
                        scene.storyboard().push_back(std::move(panel));
                    }
                }
            }
        }

        // 設定ボード(boards.ppam)。無ければ空のまま許容する
        const std::filesystem::path boardsPath = folder / kBoardsFileName;
        if (std::filesystem::exists(boardsPath)) {
            Container boardsContainer;
            if (!readContainer(boardsPath, &boardsContainer, errorOut)) return nullptr;
            if (boardsContainer.root.value("fileType", std::string()) == "boards" &&
                boardsContainer.root.contains("boards")) {
                for (const json& jBoard : boardsContainer.root.at("boards")) {
                    SettingBoard board;
                    board.name = jBoard.value("name", std::string());
                    board.finalStamp = jBoard.value("finalStamp", false);
                    if (!readBitmapBlob(jBoard, boardsContainer.blobBase(), boardsContainer.blobTotal(), &board.image,
                                        errorOut)) {
                        return nullptr;
                    }
                    board.activeLayer = jBoard.value("activeLayer", static_cast<size_t>(0));
                    if (!readPaintLayers(jBoard, boardsContainer.blobBase(), boardsContainer.blobTotal(), &board.layers,
                                         errorOut)) {
                        return nullptr;
                    }
                    if (jBoard.contains("colorSpecs")) {
                        for (const json& jSpec : jBoard.at("colorSpecs")) {
                            ColorSpec spec;
                            spec.name = jSpec.value("name", std::string());
                            spec.color.r = jSpec.value("r", static_cast<uint8_t>(0));
                            spec.color.g = jSpec.value("g", static_cast<uint8_t>(0));
                            spec.color.b = jSpec.value("b", static_cast<uint8_t>(0));
                            spec.color.a = jSpec.value("a", static_cast<uint8_t>(255));
                            board.colorSpecs.push_back(std::move(spec));
                        }
                    }
                    if (jBoard.contains("textBoxes")) {
                        for (const json& jBox : jBoard.at("textBoxes")) {
                            SettingBoardTextBox box;
                            box.text = jBox.value("text", std::string());
                            box.x = jBox.value("x", 120);
                            box.y = jBox.value("y", 120);
                            box.width = std::max(1, jBox.value("width", 640));
                            box.height = std::max(1, jBox.value("height", 160));
                            box.fontPixelSize = std::max(1, jBox.value("fontPixelSize", 48));
                            box.color.r = jBox.value("r", static_cast<uint8_t>(0));
                            box.color.g = jBox.value("g", static_cast<uint8_t>(0));
                            box.color.b = jBox.value("b", static_cast<uint8_t>(0));
                            box.color.a = jBox.value("a", static_cast<uint8_t>(255));
                            board.textBoxes.push_back(std::move(box));
                        }
                    }
                    project->settingBoards().push_back(std::move(board));
                }
            }
        }

        // パレットはオプショナル(存在しなければ空のまま)。project.ppamに格納する
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
