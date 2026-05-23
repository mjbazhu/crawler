#pragma once
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class StatementCache {
public:
    explicit StatementCache(sqlite3 *db);
    ~StatementCache();

    // 禁止拷贝与移动（缓存与特定连接地址强绑定）
    StatementCache(const StatementCache &) = delete;
    StatementCache &operator=(const StatementCache &) = delete;

    // 获取缓存的 Statement，若不存在则编译
    // 返回的指针由 Cache 所有，调用者不可 Finalize
    sqlite3_stmt *get(std::string_view sql);

    // 清空所有缓存
    void clear();

private:
    sqlite3 *db_;
    std::unordered_map<std::string, sqlite3_stmt *> cache_;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
