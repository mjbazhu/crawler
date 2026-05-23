#pragma once

#include <sqlite3.h>

#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

// SQLite schema 初始化器，只加载并执行外部 SQL 文件，不在 C++ 内硬编码表结构。
class SchemaManager {
public:
    // 初始化数据库结构（建表、建索引），schema_path 默认指向项目 sql 目录。
    static bool initialize(sqlite3 *db, std::string_view schema_path = "sql/sqlite/schema.sql");
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
