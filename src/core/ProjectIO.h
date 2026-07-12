#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "Project.h"

namespace core {

// 部分保存の指定。nullptr(既定)なら全ファイル書き出し。
// フォルダにproject.ppamが存在しない(新規保存先)場合はこの指定に関わらず全書き出しする
struct SaveOptions {
    bool writeProject = true;
    bool writeStoryboard = true;
    bool writeBoards = true;
    bool writeAllCuts = true;   // trueなら全カット(cutIdsは無視)
    std::set<uint64_t> cutIds;  // writeAllCuts=falseのとき書き出すカットID
};

// プロジェクトフォルダ(.ppproj)の保存/読み込み。フォーマットは docs/FILE_FORMAT.md を参照。
//   <作品名>.ppproj/
//     project.ppam      プロジェクト構造(軽量、シーン→カットIDの並び)
//     storyboard.ppam    絵コンテ(全シーンぶん、絵込み)
//     boards.ppam        設定ボード(絵込み)
//     cuts/cut_<ID>.ppam カット1個ずつ(作画・エフェクト・撮影設定など一式)
class ProjectIO {
public:
    static constexpr uint32_t kContainerVersion = 1;
    // v2: Cut→Cel→Layer階層。開発中は旧スキーマとの後方互換を維持しない(docs/FILE_FORMAT.md参照)
    static constexpr int kSchemaVersion = 2;

    // folderへプロジェクトフォルダとして保存する。folder(及びcuts/)は無ければ作成する。
    // 失敗時はfalseを返し、errorOutに日本語のエラー内容を格納する
    static bool save(const Project& project, const std::filesystem::path& folder, std::string* errorOut = nullptr,
                      const SaveOptions* options = nullptr);

    // pathは.ppprojフォルダ、またはその中のproject.ppamのどちらでも受け付ける。
    // 失敗時はnullptrを返し、errorOutにエラー内容を格納する
    static std::unique_ptr<Project> load(const std::filesystem::path& path, std::string* errorOut = nullptr);
};

}  // namespace core
