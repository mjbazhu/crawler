#pragma once

#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"
#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"
#include "crawler/infrastructure/music/persistence/sqlite/Transaction.h"

#include <sqlite3.h>

#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

template <typename T>
BaseDao<T>::BaseDao(ConnectionPool &pool) : pool_(pool)
{}

template <typename T>
bool BaseDao<T>::save(const T &item)
{
    auto conn = pool_.acquire();
    return doSave(conn, item);
}

template <typename T>
bool BaseDao<T>::saveBatch(const std::vector<T> &items)
{
    if (items.empty()) {
        return true;
    }

    auto conn = pool_.acquire();
    // RAII 事务管理：构造时 BEGIN，析构时若未 commit 则 ROLLBACK
    Transaction trx(conn.get());
    if (!trx.isOK()) {
        return false;
    }

    for (const auto &item : items) {
        if (!doSave(conn, item)) {
            return false; // 触发自动回滚
        }
    }

    return trx.commit();
}

template <typename T>
std::vector<T> BaseDao<T>::getAll()
{
    std::vector<T> results;
    auto conn = pool_.acquire();
    std::string sql = "SELECT * FROM " + getTableName();
    auto stmt = conn.createStatement(sql);
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(mapRowToEntity(stmt));
    }
    return results;
}

template <typename T>
std::vector<T> BaseDao<T>::getList(int offset, int limit)
{
    std::vector<T> results;
    auto conn = pool_.acquire();
    std::string sql = "SELECT * FROM " + getTableName() + " LIMIT ? OFFSET ?";
    auto stmt = conn.createStatement(sql);
    stmt.bindInt64(1, limit);
    stmt.bindInt64(2, offset);
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(mapRowToEntity(stmt));
    }
    return results;
}

template <typename T>
uint64_t BaseDao<T>::count()
{
    auto conn = pool_.acquire();
    std::string sql = "SELECT COUNT(*) FROM " + getTableName();
    auto stmt = conn.createStatement(sql);
    if (stmt.step() == SQLITE_ROW) {
        return static_cast<uint64_t>(stmt.getColumnInt64(0));
    }
    return 0;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
