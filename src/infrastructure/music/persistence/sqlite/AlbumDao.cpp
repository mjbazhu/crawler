#include "crawler/infrastructure/music/persistence/sqlite/AlbumDao.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"

#include <sqlite3.h>

#include <ctime>
#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

using domain::Album;

namespace {

// 加载 SQLite 调试后端的专辑写入 SQL，避免在 C++ 中硬编码方言语句。
const std::string &albumInsertSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/album_insert.sql");
    return sql;
}

// 加载 SQLite 调试后端的专辑完成标记 SQL。
const std::string &albumMarkDoneSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/album_mark_done.sql");
    return sql;
}

// 加载 SQLite 调试后端的专辑完成状态查询 SQL。
const std::string &albumIsDoneSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/album_is_done.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手专辑查询 SQL。
const std::string &albumSelectBySingerIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/album_select_by_singer_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手专辑计数 SQL。
const std::string &albumCountBySingerIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/album_count_by_singer_id.sql");
    return sql;
}

} // namespace

AlbumDao::AlbumDao(ConnectionPool &pool) : BaseDao<Album>(pool)
{}

bool AlbumDao::saveBatch(const std::vector<Album> &albums)
{
    return BaseDao<Album>::saveBatch(albums);
}

bool AlbumDao::doSave(PooledConnection &conn, const Album &item)
{
    const time_t now = std::time(nullptr);
    auto save_stmt = conn.createStatement(albumInsertSql());

    int idx = 1;
    // 绑定参数
    save_stmt.bindText(idx++, item.title);
    save_stmt.bindInt64(idx++, item.album_id);
    save_stmt.bindText(idx++, item.artwork);
    save_stmt.bindText(idx++, item.artist);
    save_stmt.bindInt64(idx++, item.singer_id);
    save_stmt.bindText(idx++, item.album_infos);
    save_stmt.bindInt64(idx++, item.listen_num);
    save_stmt.bindInt64(idx++, now);
    save_stmt.bindInt64(idx++, now);

    return save_stmt.step() == SQLITE_DONE;
}

std::string AlbumDao::getTableName()
{
    return "albums";
}

Album AlbumDao::mapRowToEntity(Statement &stmt)
{
    Album album;
    // 对应 SchemaManager 中的 albums 表结构
    // 0: id, 1: title, 2: album_id, 3: artwork, 4: artist, 5: singer_id, 6: album_infos, 7: listen_number
    album.title = stmt.getColumnString(1);
    album.album_id = stmt.getColumnInt64(2);
    album.artwork = stmt.getColumnString(3);
    album.artist = stmt.getColumnString(4);
    album.singer_id = stmt.getColumnInt64(5);
    album.album_infos = stmt.getColumnString(6);
    album.listen_num = stmt.getColumnInt64(7);
    album.created_at = stmt.getColumnInt64(8);
    album.updated_at = stmt.getColumnInt64(9);
    return album;
}

bool AlbumDao::markDone(const Album &item)
{
    auto conn = pool_.acquire();
    auto mark_done_stmt = conn.createStatement(albumMarkDoneSql());

    int idx = 1;
    mark_done_stmt.bindInt64(idx++, std ::time(nullptr));
    mark_done_stmt.bindInt64(idx++, item.album_id);

    return mark_done_stmt.step() == SQLITE_DONE;
}

bool AlbumDao::isDone(const Album &item)
{
    auto conn = pool_.acquire();
    auto is_done_stmt = conn.createStatement(albumIsDoneSql());

    is_done_stmt.bindInt64(1, item.album_id);
    int rc = is_done_stmt.step();

    if (rc != SQLITE_ROW) return false;

    return sqlite3_column_int(is_done_stmt.get(), 0) == 1;
}

// 获取指定歌手的专辑
std::vector<Album> AlbumDao::getBySingerId(int64_t singer_id)
{
    std::vector<Album> results;
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(albumSelectBySingerIdSql());
    stmt.bindInt64(1, singer_id);
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(mapRowToEntity(stmt));
    }
    return results;
}

// 获取指定歌手的专辑数量
uint64_t AlbumDao::countBySingerId(int64_t singer_id)
{
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(albumCountBySingerIdSql());
    stmt.bindInt64(1, singer_id);
    if (stmt.step() == SQLITE_ROW) {
        return static_cast<uint64_t>(stmt.getColumnInt64(0));
    }
    return 0;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
