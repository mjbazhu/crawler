#include "crawler/infrastructure/music/persistence/sqlite/SchemaManager.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <sqlite3.h>

#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

bool SchemaManager::initialize(sqlite3 *db, std::string_view schema_path)
{
    if (!db) {
        std::cerr << "[SchemaManager] Database connection is null" << std::endl;
        return false;
    }

    const auto resolved_path = sql_loader::SqlFileLoader::resolvePath(schema_path);
    if (!resolved_path) {
        std::cerr << "[SchemaManager] Schema SQL file not found: " << schema_path << std::endl;
        return false;
    }

    const auto sql = sql_loader::SqlFileLoader::load(resolved_path->string());
    if (!sql || sql->empty()) {
        std::cerr << "[SchemaManager] Schema SQL file is empty or unreadable: " << resolved_path->string() << std::endl;
        return false;
    }

    char *err_msg = nullptr;
    if (sqlite3_exec(db, sql->c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[SchemaManager] Schema SQL execution failed: " << (err_msg ? err_msg : "Unknown") << std::endl;
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }

    std::cout << "[SchemaManager] Database schema initialized from " << resolved_path->string() << std::endl;
    return true;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
