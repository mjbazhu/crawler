#include "crawler/infrastructure/music/persistence/sqlite/SchemaManager.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <sqlite3.h>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;
namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;

namespace {

// 记录 SQLite schema 初始化日志，避免裸控制台输出破坏运行日志格式。
void logSchema(logging::LogLevel level, std::string_view message)
{
    logging::Logger::instance().log(level, "SchemaManager", message);
}

} // namespace

bool SchemaManager::initialize(sqlite3 *db, std::string_view schema_path)
{
    if (!db) {
        logSchema(logging::LogLevel::L_ERROR, "Database connection is null");
        return false;
    }

    const auto resolved_path = sql_loader::SqlFileLoader::resolvePath(schema_path);
    if (!resolved_path) {
        logSchema(logging::LogLevel::L_ERROR, "Schema SQL file not found: " + std::string(schema_path));
        return false;
    }

    const auto sql = sql_loader::SqlFileLoader::load(resolved_path->string());
    if (!sql || sql->empty()) {
        logSchema(logging::LogLevel::L_ERROR, "Schema SQL file is empty or unreadable: " + resolved_path->string());
        return false;
    }

    char *err_msg = nullptr;
    if (sqlite3_exec(db, sql->c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        logSchema(logging::LogLevel::L_ERROR, std::string("Schema SQL execution failed: ") + (err_msg ? err_msg : "Unknown"));
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }

    logSchema(logging::LogLevel::L_INFO, "Database schema initialized from " + resolved_path->string());
    return true;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
