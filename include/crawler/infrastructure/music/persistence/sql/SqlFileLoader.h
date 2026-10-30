#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql {

// SQL 文件加载器，负责从项目 sql 目录或运行目录向上查找并读取外置 SQL。
class SqlFileLoader {
public:
    // 解析 SQL 文件路径，兼容源码目录、build 目录和绝对路径运行。
    static std::optional<std::filesystem::path> resolvePath(std::string_view path);

    // 读取完整 SQL 文件；文件不存在或不可读时返回 std::nullopt。
    static std::optional<std::string> load(std::string_view path);

    // 读取完整 SQL 文件；失败时抛出 std::runtime_error，适合启动阶段硬失败。
    static std::string loadOrThrow(std::string_view path);

    // 将 SQL 脚本拆成单条语句，忽略注释和字符串里的分号。
    static std::vector<std::string> splitStatements(std::string_view script);

    // 迁移专用分句，支持独立行 DELIMITER 和复合触发器；格式错误时拒绝整个脚本。
    static std::vector<std::string> splitMigrationStatements(std::string_view script);
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql
