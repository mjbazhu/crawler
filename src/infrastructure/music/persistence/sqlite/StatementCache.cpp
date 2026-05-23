#include "crawler/infrastructure/music/persistence/sqlite/StatementCache.h"
#include <sqlite3.h>
#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

StatementCache::StatementCache(sqlite3 *db) : db_(db)
{
}

StatementCache::~StatementCache()
{
    clear();
}

sqlite3_stmt *StatementCache::get(std::string_view sql)
{
    std::string key(sql);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }

    sqlite3_stmt *stmt = nullptr;
    // 使用长度参数，支持 string_view 非空终止的情况
    int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB Error] Prepare failed in cache: " << sqlite3_errmsg(db_) << "\nSQL: " << sql << std::endl;
        return nullptr;
    }

    cache_[key] = stmt;
    return stmt;
}

void StatementCache::clear()
{
    for (auto &pair : cache_) {
        sqlite3_finalize(pair.second);
    }
    cache_.clear();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
