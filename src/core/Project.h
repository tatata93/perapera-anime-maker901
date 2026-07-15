#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "Scene.h"

namespace core {

// 色指定1色(色指定書の「肌」「髪 影」などの項目)。作画・彩色中に参照ドックから
// この色を拾ってそのまま塗れるようにするための名前付き色見本
struct ColorSpec {
    std::string name;
    Bitmap::Pixel color{0, 0, 0, 255};
};

// 設定ボード1枚(キャラ設定・美術設定などの資料)。手描きと画像貼り付けを1枚のビットマップに合成する。
// カット/シーンとは独立して存在し、作画中にいつでも参照できる資料集(プロジェクト直下で管理)
struct SettingBoard {
    std::string name;  // 例「キャラ: 主人公」「美術: 教室」
    Bitmap image;       // ボードの中身(1920x1080、透明下地)
    std::vector<ColorSpec> colorSpecs;  // 色指定書(肌/髪/影など名前付きの色見本)。既定は空
    bool finalStamp = false;
};

// アニメーション制作プロジェクト全体。シーンを順序付きで保持する。
class Project {
public:
    explicit Project(std::string name = "Untitled") : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Scene& addScene(std::string name);
    void removeScene(size_t index);

    size_t sceneCount() const { return m_scenes.size(); }
    Scene& scene(size_t index) { return *m_scenes.at(index); }
    const Scene& scene(size_t index) const { return *m_scenes.at(index); }

    // カラーパレット(登録色の一覧)。既定は空
    std::vector<Bitmap::Pixel>& palette() { return m_palette; }
    const std::vector<Bitmap::Pixel>& palette() const { return m_palette; }

    // 設定ボード(キャラ・美術などの資料集)。既定は空
    std::vector<SettingBoard>& settingBoards() { return m_settingBoards; }
    const std::vector<SettingBoard>& settingBoards() const { return m_settingBoards; }

    // カットの永続ID採番カウンタ(既定1)。ProjectIO::saveがid==0のカットへ
    // ここから採番し、採番後にこのカウンタを更新する
    uint64_t nextCutId() const { return m_nextCutId; }
    void setNextCutId(uint64_t id) { m_nextCutId = id; }

    // キャンバス解像度(既定1920x1080=フルHD)。新規セル・合成・書き出しのサイズに使う。
    // 変更しても既存の作画セル(引きセルのpaperサイズ)は変わらない
    int canvasWidth() const { return m_canvasWidth; }
    int canvasHeight() const { return m_canvasHeight; }
    // 幅/高さをそれぞれ[kMinCanvasSize, kMaxCanvasSize]にクランプして設定する
    void setCanvasSize(int width, int height);

private:
    std::string m_name;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<Bitmap::Pixel> m_palette;
    std::vector<SettingBoard> m_settingBoards;
    uint64_t m_nextCutId = 1;
    int m_canvasWidth = 1920;
    int m_canvasHeight = 1080;
};

}  // namespace core
