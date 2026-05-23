#include "crawler/infrastructure/music/persistence/sqlite/SongDao.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"
#include "crawler/infrastructure/music/persistence/sqlite/Transaction.h"

#include <sqlite3.h>

#include <ctime>
#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

using domain::Song;

namespace {

// 加载 SQLite 调试后端的歌曲写入 SQL，避免在 C++ 中硬编码方言语句。
const std::string &songInsertSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_insert.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌曲专辑 ID 更新 SQL。
const std::string &songUpdateAlbumIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_update_album_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手歌曲查询 SQL。
const std::string &songSelectBySingerIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_select_by_singer_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌手歌曲计数 SQL。
const std::string &songCountBySingerIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_count_by_singer_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的专辑歌曲计数 SQL。
const std::string &songCountByAlbumIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_count_by_album_id.sql");
    return sql;
}

// 加载 SQLite 调试后端的歌曲 ID 查询 SQL。
const std::string &songFindByIdSql()
{
    static const std::string sql = sql::SqlFileLoader::loadOrThrow("sql/sqlite/song_find_by_id.sql");
    return sql;
}

} // namespace

SongDao::SongDao(ConnectionPool &pool) : BaseDao<Song>(pool)
{}

bool SongDao::saveBatch(const std::vector<Song> &songs)
{
    return BaseDao<Song>::saveBatch(songs);
}

// 保存歌曲
bool SongDao::doSave(PooledConnection &conn, const Song &item)
{
    const time_t now = std::time(nullptr);
    auto save_stmt = conn.createStatement(songInsertSql());

    int idx = 1;
    if (save_stmt.bindText(idx++, item.title) != SQLITE_OK) return false;
    if (save_stmt.bindInt64(idx++, item.song_id) != SQLITE_OK) return false;
    if (save_stmt.bindText(idx++, item.artist) != SQLITE_OK) return false;
    if (save_stmt.bindInt64(idx++, item.singer_id) != SQLITE_OK) return false;
    if (save_stmt.bindText(idx++, item.album) != SQLITE_OK) return false;
    if (save_stmt.bindInt64(idx++, item.album_id) != SQLITE_OK) return false;
    if (save_stmt.bindText(idx++, item.artwork) != SQLITE_OK) return false;
    if (save_stmt.bindText(idx++, item.lrc) != SQLITE_OK) return false;
    if (save_stmt.bindInt64(idx++, now) != SQLITE_OK) return false;
    if (save_stmt.bindInt64(idx++, now) != SQLITE_OK) return false;

    return save_stmt.step() == SQLITE_DONE;
}

// 获取表名
std::string SongDao::getTableName()
{
    return "songs";
}

// 映射行
Song SongDao::mapRowToEntity(Statement &stmt)
{
    Song song;
    // 对应 SchemaManager 中的 songs 表结构 (SELECT * 返回顺序)
    // 0: id, 1: title, 2: song_id, 3: artist, 4: singer_id, 5: album,
    // 6: album_id, 7: artwork, 8: lrc, 9: created_at, 10: updated_at
    song.title = stmt.getColumnString(1);
    song.song_id = stmt.getColumnInt64(2);
    song.artist = stmt.getColumnString(3);
    song.singer_id = stmt.getColumnInt64(4);
    song.album = stmt.getColumnString(5);
    song.album_id = stmt.getColumnInt64(6);
    song.artwork = stmt.getColumnString(7);
    song.lrc = stmt.getColumnString(8);
    song.created_at = stmt.getColumnInt64(9);
    song.updated_at = stmt.getColumnInt64(10);
    return song;
}

// 插入专辑id
bool SongDao::insertAlbumId(int64_t album_id, std::vector<Song> &songs)
{
    if (songs.empty()) {
        return false;
    }

    auto conn = pool_.acquire();
    // 开启事务进行批量更新
    Transaction trx(conn.get());
    if (!trx.isOK()) {
        return false;
    }

    std::vector<int> pending_removal_indices;
    pending_removal_indices.reserve(songs.size());

    bool any_failure = false;
    // 倒序遍历，以便安全删除元素
    for (int i = songs.size() - 1; i >= 0; i--) {
        auto save_albumId_stmt = conn.createStatement(songUpdateAlbumIdSql());

        int idx = 1;
        bool bind_ok = true;
        if (save_albumId_stmt.bindInt64(idx++, album_id) != SQLITE_OK) bind_ok = false;
        if (save_albumId_stmt.bindInt64(idx++, songs[i].song_id) != SQLITE_OK) bind_ok = false;

        if (!bind_ok) {
            any_failure = true;
            continue;
        }

        // 使用 conn.get() 获取 db 句柄以检查 changes
        if (save_albumId_stmt.step() == SQLITE_DONE && sqlite3_changes(conn.get()) > 0) {
            pending_removal_indices.push_back(i);
            std::cout << "title: " << songs[i].title << "\t\t\t专辑ID: " << album_id << "\t\t歌曲存在，更新成功" << std::endl;
        } else {
            std::cout << "title: " << songs[i].title << "\t\t\t专辑ID: " << album_id << "\t\t歌曲不存在，更新失败" << std::endl;
            any_failure = true;
        }
    }

    // 提交事务（即使部分失败也提交已成功的操作，或者根据业务需求决定）
    // 此处逻辑：只要事务提交成功即返回 true，具体业务层面的全部成功与否由 any_failure 反映
    // 但根据接口定义，通常返回事务提交结果
    if (trx.commit()) {
        for (int i : pending_removal_indices) {
            songs.erase(songs.begin() + i);
        }
        return true;
    }
    return false;
}

// 根据歌手ID查询歌曲
std::vector<Song> SongDao::getBySingerId(int64_t singer_id)
{
    std::vector<Song> results;
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(songSelectBySingerIdSql());
    stmt.bindInt64(1, singer_id);
    while (stmt.step() == SQLITE_ROW) {
        results.push_back(mapRowToEntity(stmt));
    }
    return results;
}

// 根据歌手ID查询歌曲数量
uint64_t SongDao::countBySingerId(int64_t singer_id)
{
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(songCountBySingerIdSql());
    stmt.bindInt64(1, singer_id);
    if (stmt.step() == SQLITE_ROW) {
        return static_cast<uint64_t>(stmt.getColumnInt64(0));
    }
    return 0;
}

// 根据专辑ID查询歌曲数量
uint64_t SongDao::countByAlbumId(int64_t album_id)
{
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(songCountByAlbumIdSql());
    stmt.bindInt64(1, album_id);
    if (stmt.step() == SQLITE_ROW) {
        return static_cast<uint64_t>(stmt.getColumnInt64(0));
    }
    return 0;
}

// 根据歌曲ID查询歌曲
Song SongDao::getBySongId(int64_t id)
{
    auto conn = pool_.acquire();
    auto stmt = conn.createStatement(songFindByIdSql());
    stmt.bindInt64(1, id);
    return stmt.step() == SQLITE_ROW ? mapRowToEntity(stmt) : Song();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
