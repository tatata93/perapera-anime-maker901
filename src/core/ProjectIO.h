#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "Project.h"

namespace core {

// .ppamファイルの保存/読み込み。フォーマットは docs/FILE_FORMAT.md を参照。
class ProjectIO {
public:
    static constexpr uint32_t kContainerVersion = 1;
    // v2: Cut→Cel→Layer階層。開発中は旧スキーマとの後方互換を維持しない(docs/FILE_FORMAT.md参照)
    static constexpr int kSchemaVersion = 2;

    // 失敗時はfalseを返し、errorOutに日本語のエラー内容を格納する
    static bool save(const Project& project, const std::filesystem::path& path, std::string* errorOut = nullptr);

    // 失敗時はnullptrを返し、errorOutにエラー内容を格納する
    static std::unique_ptr<Project> load(const std::filesystem::path& path, std::string* errorOut = nullptr);
};

}  // namespace core
