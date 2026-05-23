#include "migu/support/MySqlTestSupport.h"

#include "crawler/application/model/SongSourceMetadata.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace support = crawler::tests::migu::support;

namespace {

// 测试断言失败时抛出明确错误，避免 assert 宏影响 clangd 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 为专辑 identity_hash 生成 64 字符唯一值。
std::string identityHash(std::string suffix)
{
    std::string value = "codexrelation" + std::move(suffix);
    value.resize(64, '0');
    return value;
}

// 生成跨测试运行唯一的后缀。
std::string uniqueSuffix()
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<long long>(ticks));
}

// 执行 SQL 并忽略结果。
boost::asio::awaitable<void> executeSql(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    (void)co_await pool->coExecute(std::move(sql));
}

// 执行 COUNT 类 SQL。
boost::asio::awaitable<std::uint64_t> scalarCount(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    const auto result = co_await pool->coExecute(std::move(sql));
    if (result.rows().empty()) {
        co_return 0;
    }
    co_return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 删除指定唯一后缀的关系和来源数据，避免并行测试互相清理。
boost::asio::awaitable<void> cleanupRelationFixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &suffix)
{
    co_await executeSql(pool, "DELETE r FROM song_source_refs r JOIN songs s ON s.id = r.song_db_id WHERE r.source = 'migu' AND s.normalized_title = 'codex_relation_song_" + suffix + "'");
    co_await executeSql(pool, "DELETE FROM songs WHERE normalized_title = 'codex_relation_song_" + suffix + "'");
    co_await executeSql(pool, "DELETE FROM album_source_refs WHERE source = 'migu' AND source_item_key = 'codex_relation_album:" + suffix + "'");
    co_await executeSql(pool, "DELETE FROM albums WHERE normalized_title = 'codex_relation_album_" + suffix + "'");
    co_await executeSql(pool, "DELETE FROM singer_source_refs WHERE source = 'migu' AND source_item_key = 'codex_relation_singer:" + suffix + "'");
    co_await executeSql(pool, "DELETE FROM singers WHERE normalized_name = 'codex_relation_artist_" + suffix + "'");
}

// 测试结束时兜底清理 fixture。
class FixtureGuard {
public:
    // 保存连接池和唯一后缀，析构时只清理本测试数据。
    FixtureGuard(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::string suffix) : pool_(std::move(pool)),
                                                                                         suffix_(std::move(suffix))
    {}

    // 执行兜底清理；析构不抛异常。
    ~FixtureGuard()
    {
        try {
            if (pool_) {
                support::runOnExecutor(pool_->executor(), cleanupRelationFixtures(pool_, suffix_), "cleanup relation fixtures");
            }
        } catch (...) {
        }
    }

private:
    std::shared_ptr<mysql::MySqlConnectionPool> pool_; // 测试连接池
    std::string suffix_;                               // 本轮测试唯一后缀
};

// 插入一个专辑来源，使 album_song_refs 能回填 album_source_ref_id。
boost::asio::awaitable<std::string> seedAlbumSource(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &suffix)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const std::string album_id = "880" + suffix;
    const std::string album_title = "Codex Relation Album " + suffix;
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;

    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, primary_artist_name, release_date, identity_hash, created_at, updated_at) VALUES ('" + album_title + "', 'codex_relation_album_" + suffix + "', 'Codex Relation Artist', '2026-05-20', '" + identityHash(suffix) + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_item_key, source_album_id, source_album_kind, source_title, source_artist_id, source_artist_name, detail_sync_status, songs_sync_status, created_at, updated_at) VALUES (" + std::to_string(album_db_id) + ", 'migu', 'codex_relation_album:" + suffix + "', '" + album_id + "', 1, '" + album_title + "', 'artist-primary-" + suffix + "', 'Codex Relation Artist', 2, 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_return album_id;
}

// 插入入口歌手来源，使 song_artist_refs 能关联 singer_source_refs.id。
boost::asio::awaitable<void> seedSingerSource(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &suffix)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const std::string artist_id = "artist-primary-" + suffix;
    const std::string artist_name = "Codex Primary Artist " + suffix;
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;

    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + artist_name + "', 'codex_relation_artist_" + suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_item_key, source_singer_id, source_name_pinyin, source_country, source_tag, source_metric, songs_sync_status, albums_sync_status, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', 'codex_relation_singer:" + suffix + "', '" + artist_id + "', 'codex', 'huayu', 'test', '1', 2, 2, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
}

// 构造覆盖多歌手、Disc/track 和完整音质参数的歌曲来源。
model::SongSourceMetadata makeSongFixture(const std::string &suffix, const std::string &album_id)
{
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "770" + suffix;
    song.source_content_id = "670" + suffix;
    song.source_resource_type = "2";
    song.title = "Codex Relation Song " + suffix;
    song.normalized_title = "codex_relation_song_" + suffix;
    song.source_duration_seconds = 240;
    song.source_album_id = album_id;
    song.source_album_name = "Codex Relation Album " + suffix;
    song.source_primary_artist_id = "artist-primary-" + suffix;
    song.source_primary_artist_name = "Codex Primary Artist " + suffix;
    song.cover_url = "https://example.invalid/relation-cover.webp";
    song.lyric_url = "https://example.invalid/relation.lrc";
    song.mrc_url = "https://example.invalid/relation.mrc";
    song.trc_url = "https://example.invalid/relation.trc";
    song.copyright_id = "copyright-" + suffix;
    song.album_disc_name = "Disc 2";
    song.album_disc_no = 2;
    song.album_track_no = 7;
    song.album_song_order = 7;

    song.artist_refs.push_back({"artist-primary-" + suffix, "Codex Primary Artist " + suffix, "https://example.invalid/primary.webp", "primary", "artist", 1, true, true});
    song.artist_refs.push_back({"artist-guest-" + suffix, "Codex Guest Artist " + suffix, "https://example.invalid/guest.webp", "guest", "artist", 2, false, false});

    song.audio_formats.push_back({"PQ", "2", "020007", "020007", 3844182, 3844182});
    song.audio_formats.push_back({"HQ", "2", "020010", "020010", 9610138, 9610138});
    song.audio_formats.push_back({"SQ", "E", "011003", "011002", 26738823, 26215426});
    song.audio_formats.push_back({"ZQ24", "2", "011006", "011005", 51364112, 50326914});
    song.audio_formats.push_back({"Z3D", "2", "020025", "020024", 27096333, 69158564});
    return song;
}

// 验证关系表、资源队列和查询能力。
void assertSongRelations(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, mysql::MySqlMusicCatalogQueryRepository &catalog_repository, const std::string &suffix, const std::string &album_id)
{
    const std::string source_content_id = "670" + suffix;
    const auto artist_refs = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "'"), "count song artist refs");
    require(artist_refs == 2, "expected two song artist refs");

    const auto linked_seed_refs = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND ar.source_artist_id = 'artist-primary-" + suffix + "' AND ar.is_seed = 1 AND ar.singer_source_ref_id IS NOT NULL"), "count linked seed artist refs");
    require(linked_seed_refs == 1, "expected seed artist ref linked to singer_source_refs");

    const auto unresolved_guest_refs = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND ar.source_artist_id = 'artist-guest-" + suffix + "' AND ar.singer_source_ref_id IS NULL"), "count unresolved guest artist refs");
    require(unresolved_guest_refs == 1, "expected unknown guest artist to keep source id/name without singer_source_refs link");

    const auto album_refs = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM album_song_refs abr JOIN song_source_refs r ON r.id = abr.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND abr.source_album_id = '" + album_id + "' AND abr.disc_no = 2 AND abr.track_no = 7"), "count album song refs");
    require(album_refs == 1, "expected one album song ref with disc and track");

    const auto format_refs = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_source_audio_formats f JOIN song_source_refs r ON r.id = f.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND f.format_type IN ('PQ','HQ','SQ','ZQ24','Z3D') AND f.resource_type <> '' AND f.iformat <> '' AND f.aformat <> '' AND f.isize > 0 AND f.asize > 0"), "count complete audio formats");
    require(format_refs == 5, "expected five complete audio format refs");

    const auto audio_resources = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_resources sr JOIN song_source_refs r ON r.id = sr.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND sr.resource_kind = 'audio' AND sr.format_type IN ('PQ','HQ','SQ','ZQ24','Z3D')"), "count audio resources");
    require(audio_resources == 5, "expected five audio resource tasks");

    const auto metadata_resources = support::runOnExecutor(pool->executor(), scalarCount(pool, "SELECT COUNT(*) FROM song_resources sr JOIN song_source_refs r ON r.id = sr.song_source_ref_id WHERE r.source_content_id = '" + source_content_id + "' AND sr.resource_kind IN ('cover','lyric','mrc','trc')"), "count metadata resources");
    require(metadata_resources == 4, "expected cover, lyric, mrc and trc resource tasks");

    model::MiguSongCatalogQuery coartist_query;
    coartist_query.source_singer_id = "artist-guest-" + suffix;
    const auto coartist_songs = catalog_repository.listSongs(coartist_query);
    require(coartist_songs.total == 1, "coartist query should use song_artist_refs");

    model::MiguSongCatalogQuery album_query;
    album_query.source_album_id = album_id;
    const auto album_songs = catalog_repository.listSongs(album_query);
    require(album_songs.total == 1, "album query should use album_song_refs");
}

} // namespace

int main()
{
    try {
        auto pool = support::makeInitializedPool();
        const std::string suffix = uniqueSuffix();
        FixtureGuard guard(pool, suffix);
        support::runOnExecutor(pool->executor(), cleanupRelationFixtures(pool, suffix), "initial relation fixture cleanup");

        support::runOnExecutor(pool->executor(), seedSingerSource(pool, suffix), "seed singer source");
        const std::string album_id = support::runOnExecutor(pool->executor(), seedAlbumSource(pool, suffix), "seed album source");
        mysql::MySqlSongSourceRepository song_repository(pool);
        mysql::MySqlMusicCatalogQueryRepository catalog_repository(pool);

        const auto song = makeSongFixture(suffix, album_id);
        require(song_repository.saveBatch({song}), "song source saveBatch should succeed");
        assertSongRelations(pool, catalog_repository, suffix, album_id);

        const auto schema_status = catalog_repository.schemaStatus();
        require(schema_status.ok, "schema status should be ok");
        require(schema_status.schema_version >= 2026052001, "schema version should include relation schema");

        std::cout << "MiguSongRelationIntegrationTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongRelationIntegrationTest failed: " << e.what() << std::endl;
        return 1;
    }
}
