#include "crawler/infrastructure/music/persistence/sqlite/SingerDao.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"

#include <sqlite3.h>

#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

using domain::Gender;
using domain::Singer;

namespace {

// 加载 SQLite 调试后端的歌手写入 SQL，避免在 C++ 中硬编码方言语句。
const std::string &singerInsertSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/singer_insert.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手完成状态查询 SQL。
const std::string &singerIsDoneSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/singer_is_done.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手完成标记 SQL。
const std::string &singerMarkDoneSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/singer_mark_done.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手 ID 查询 SQL。
const std::string &singerSelectBySingerIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/singer_select_by_singer_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的待处理歌手名称查询 SQL。
const std::string &singerSelectNameStatusSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/singer_select_name_status.sql");
    return sql;
}

} // namespace

SingerDao::SingerDao(ConnectionPool &pool) : BaseDao<Singer>(pool)
{}

bool SingerDao::saveBatch(const std::vector<Singer> &singers)
{
    return BaseDao<Singer>::saveBatch(singers);
}

bool SingerDao::doSave(PooledConnection &conn, const Singer &item)
{
    const time_t now = time(nullptr);
    auto save_stmt = conn.createStatement(singerInsertSql());

    int idx = 1;
    save_stmt.bindText(idx++, item.name);
    save_stmt.bindInt64(idx++, item.singer_id);
    save_stmt.bindText(idx++, item.artwork);
    save_stmt.bindText(idx++, item.country);
    save_stmt.bindInt64(idx++, static_cast<int>(item.gender));
    save_stmt.bindText(idx++, item.genre);
    save_stmt.bindInt64(idx++, now);
    save_stmt.bindInt64(idx++, now);

    return save_stmt.step() == SQLITE_DONE;
}

std::string SingerDao::getTableName()
{
    return "singers";
}

Singer SingerDao::mapRowToEntity(Statement &stmt)
{
    Singer singer;
    // 对应 SchemaManager 中的 singers 表结构
    // 0: id, 1: name, 2: singer_id, 3: artwork, 4: genre, 5: country, 6: gender
    singer.name = stmt.getColumnString(1);
    singer.singer_id = stmt.getColumnInt64(2);
    singer.artwork = stmt.getColumnString(3);
    singer.genre = stmt.getColumnString(4);
    singer.country = stmt.getColumnString(5);
    singer.gender = static_cast<Gender>(stmt.getColumnInt64(6));
    singer.created_at = stmt.getColumnInt64(7);
    singer.updated_at = stmt.getColumnInt64(8);
    return singer;
}

bool SingerDao::isDone(int64_t singer_id)
{
    auto conn = pool_.acquire();
    auto is_done_stmt = conn.createStatement(singerIsDoneSql());

    is_done_stmt.bindInt64(1, singer_id);
    int rc = is_done_stmt.step();

    if (rc != SQLITE_ROW) return false;

    return sqlite3_column_int(is_done_stmt.get(), 0) == 1;
}

bool SingerDao::markDone(int64_t singer_id)
{
    auto conn = pool_.acquire();
    auto mark_done_stmt = conn.createStatement(singerMarkDoneSql());

    int idx = 1;
    mark_done_stmt.bindInt64(idx++, time(nullptr));
    mark_done_stmt.bindInt64(idx++, singer_id);

    return mark_done_stmt.step() == SQLITE_DONE;
}

std::vector<Singer> SingerDao::getBySingerId(int64_t singer_id)
{
    std::vector<Singer> results;
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(singerSelectBySingerIdSql());
    stmt.bindInt64(1, singer_id);
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(mapRowToEntity(stmt));
    }
    return results;
}

std::vector<std::string> SingerDao::getNameByStatus()
{
    std::vector<std::string> results;
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(singerSelectNameStatusSql());
    // stmt.bindInt64(1, singer_id);
    // std::string name = stmt.getColumnString(0);
    // stmt.get()
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(stmt.getColumnString(0));
    }
    return results;
}

std::string SingerDao::getArtwork(int64_t singer_id)
{
    return getBySingerId(singer_id)[0].artwork;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
