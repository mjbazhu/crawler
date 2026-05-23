#include "migu/support/MySqlTestSupport.h"

#include "crawler/application/model/MusicCatalog.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
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

// 测试断言失败时抛出明确错误，避免 assert 宏影响 clangd tweak 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 测试 fixture 生成的来源 ID 集合。
struct CatalogFixture {
    std::string suffix;           // 唯一后缀
    std::string singer_source_id; // 平台歌手 ID
    std::string album_source_id;  // 平台专辑 ID
    std::string song_source_id;   // 第一个平台歌曲 ID
    std::string content_id;       // 第一个平台内容 ID
};

// 为专辑 identity_hash 生成 64 字符唯一值。
std::string identityHash(std::string suffix)
{
    std::string value = "codexcatalog" + std::move(suffix);
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

// 删除所有 catalog 集成测试 fixture。
boost::asio::awaitable<void> cleanupCatalogFixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    co_await executeSql(pool, "DELETE FROM song_source_refs WHERE source = 'migu' AND source_item_key LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM songs WHERE normalized_title LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM album_source_refs WHERE source = 'migu' AND source_item_key LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM albums WHERE normalized_title LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM singer_source_refs WHERE source = 'migu' AND source_item_key LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM singers WHERE normalized_name LIKE 'codex_catalog_%'");
}

// 在测试结束时清理 fixture，避免污染本地库。
class FixtureGuard {
public:
    // 保存连接池，析构时清理测试数据。
    explicit FixtureGuard(std::shared_ptr<mysql::MySqlConnectionPool> pool) : pool_(std::move(pool))
    {}

    // 执行兜底清理；析构不抛异常。
    ~FixtureGuard()
    {
        try {
            if (pool_) {
                support::runOnExecutor(pool_->executor(), cleanupCatalogFixtures(pool_), "cleanup catalog fixtures");
            }
        } catch (...) {
        }
    }

private:
    std::shared_ptr<mysql::MySqlConnectionPool> pool_; // 测试连接池
};

// 插入一组歌手、专辑、歌曲和资源队列 fixture。
boost::asio::awaitable<CatalogFixture> seedCatalogFixture(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    CatalogFixture fixture;
    fixture.suffix = uniqueSuffix();
    fixture.singer_source_id = "910" + fixture.suffix;
    fixture.album_source_id = "810" + fixture.suffix;
    fixture.song_source_id = "710" + fixture.suffix;
    fixture.content_id = "610" + fixture.suffix;

    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;

    const std::string singer_name = "Codex Catalog Singer " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + singer_name + "', 'codex_catalog_singer_" + fixture.suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_item_key, source_singer_id, source_name_pinyin, source_country, source_tag, source_metric, avatar_url, songs_sync_status, songs_sync_error, albums_sync_status, albums_sync_error, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', 'codex_catalog_singer:" + fixture.suffix + "', '" + fixture.singer_source_id + "', 'codex', 'huayu', 'test', '1', 'https://example.invalid/singer.webp', 3, 'fixture songs failed', 4, 'fixture albums partial', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    const std::string album_title = "Codex Catalog Album " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, primary_artist_name, release_date, identity_hash, created_at, updated_at) VALUES ('" + album_title + "', 'codex_catalog_album_" + fixture.suffix + "', '" + singer_name + "', '2026-01-01', '" + identityHash(fixture.suffix) + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_item_key, source_album_id, source_resource_type, source_res_type, source_action, source_album_kind, source_title, source_artist_id, source_artist_name, cover_url, total_count, release_date, publish_date, play_num, play_num_desc, detail_sync_status, detail_sync_error, songs_sync_status, songs_sync_count, songs_sync_error, created_at, updated_at) VALUES (" + std::to_string(album_db_id) + ", 'migu', 'codex_catalog_album:" + fixture.suffix + "', '" + fixture.album_source_id + "', '5', '1', 'mgmusic://digital-album-info?id=" + fixture.album_source_id + "', 2, '" + album_title + "', '" + fixture.singer_source_id + "', '" + singer_name + "', 'https://example.invalid/album.webp', '2', '2026-01-01', '2026-01-02', 9, '9', 3, 'fixture detail failed', 4, 1, 'fixture songs partial', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    const std::string song_title = "Codex Catalog Song " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO songs (title, normalized_title, created_at, updated_at) VALUES ('" + song_title + "', 'codex_catalog_song_" + fixture.suffix + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_source_refs (song_db_id, source, source_item_key, source_song_id, source_content_id, source_resource_type, source_title_pinyin, source_duration_seconds, source_album_id, source_album_name, cover_url, lyric_url, mrc_url, trc_url, copyright_id, metadata_status, created_at, updated_at) VALUES (" + std::to_string(song_db_id) + ", 'migu', 'codex_catalog_song:" + fixture.suffix + "', '" + fixture.song_source_id + "', '" + fixture.content_id + "', '2', 'codexcatalogsong', 180, '" + fixture.album_source_id + "', '" + album_title + "', 'https://example.invalid/song.webp', 'https://example.invalid/song.lrc', 'https://example.invalid/song.mrc', 'https://example.invalid/song.trc', 'copyright-" + fixture.suffix + "', 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_ref_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, source, source_artist_id, source_artist_name, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", (SELECT id FROM singer_source_refs WHERE source = 'migu' AND source_singer_id = '" + fixture.singer_source_id + "' LIMIT 1), 'migu', '" + fixture.singer_source_id + "', '" + singer_name + "', 'artist', 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO song_source_audio_formats (song_source_ref_id, format_type, resource_type, iformat, aformat, isize, asize, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", 'HQ', '2', '020010', '020010', 123, 123, " + std::to_string(now) + ", " + std::to_string(now) + "), (" + std::to_string(song_ref_id) + ", 'SQ', '2', '011003', '011002', 456, 456, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO song_resources (song_source_ref_id, resource_kind, format_type, remote_url, local_path, size_bytes, checksum, status, retry_count, last_error, error_category, result_code, fetched_at, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", 'audio', 'HQ', '', '', 0, '', 3, 2, 'fixture failed', 'network', 'network_failed', 0, " + std::to_string(now) + ", " + std::to_string(now) + "), (" + std::to_string(song_ref_id) + ", 'audio', 'SQ', '', '', 0, '', 1, 1, '', NULL, NULL, 0, " + std::to_string(now) + ", " + std::to_string(now - 7200) + "), (" + std::to_string(song_ref_id) + ", 'audio', 'PQ', '', '', 0, '', 4, 0, 'fixture skipped', 'resource', 'digital_album', 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    const std::string song_two_id = "711" + fixture.suffix;
    const std::string content_two_id = "611" + fixture.suffix;
    const std::string song_two_title = "Codex Catalog Song Missing Resource " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO songs (title, normalized_title, created_at, updated_at) VALUES ('" + song_two_title + "', 'codex_catalog_song_missing_" + fixture.suffix + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_two_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_source_refs (song_db_id, source, source_item_key, source_song_id, source_content_id, source_resource_type, source_title_pinyin, source_duration_seconds, source_album_id, source_album_name, metadata_status, created_at, updated_at) VALUES (" + std::to_string(song_two_db_id) + ", 'migu', 'codex_catalog_song_missing:" + fixture.suffix + "', '" + song_two_id + "', '" + content_two_id + "', '2', 'codexcatalogsongmissing', 181, '" + fixture.album_source_id + "', '" + album_title + "', 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_two_ref_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, source, source_artist_id, source_artist_name, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_two_ref_id) + ", (SELECT id FROM singer_source_refs WHERE source = 'migu' AND source_singer_id = '" + fixture.singer_source_id + "' LIMIT 1), 'migu', '" + fixture.singer_source_id + "', '" + singer_name + "', 'artist', 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_return fixture;
}

// 验证列表、详情、资源队列和 integrity 聚合。
void assertCatalogQueries(mysql::MySqlMusicCatalogQueryRepository &repository, const CatalogFixture &fixture, const model::MiguIntegritySummary &before)
{
    model::MiguSingerCatalogQuery singer_query;
    singer_query.keyword = fixture.singer_source_id;
    auto singers = repository.listSingers(singer_query);
    require(singers.total == 1, "expected one singer fixture");
    require(singers.items.front().source_singer_id == fixture.singer_source_id, "unexpected singer source id");
    require(repository.findSinger("migu", fixture.singer_source_id).has_value(), "findSinger should find fixture");

    model::MiguAlbumCatalogQuery album_query;
    album_query.keyword = fixture.album_source_id;
    album_query.album_kind = 2;
    album_query.detail_sync_statuses = {3};
    auto albums = repository.listAlbums(album_query);
    require(albums.total == 1, "expected one album fixture");
    require(albums.items.front().source_album_id == fixture.album_source_id, "unexpected album source id");
    require(repository.findAlbum("migu", fixture.album_source_id).has_value(), "findAlbum should find fixture");

    model::MiguSongCatalogQuery song_query;
    song_query.source_singer_id = fixture.singer_source_id;
    auto songs = repository.listSongs(song_query);
    require(songs.total == 2, "expected two song fixtures");

    song_query.format_type = "HQ";
    song_query.resource_statuses = {3};
    auto hq_failed_songs = repository.listSongs(song_query);
    require(hq_failed_songs.total == 1, "expected one failed HQ song fixture");
    require(repository.findSong("migu", fixture.song_source_id).has_value(), "findSong should find fixture");

    model::MiguResourceQueueQuery resource_query;
    resource_query.keyword = fixture.song_source_id;
    resource_query.statuses = {3};
    resource_query.resource_kind = "audio";
    resource_query.format_type = "HQ";
    auto resources = repository.listResources(resource_query);
    require(resources.total == 1, "expected one resource fixture");
    require(resources.items.front().status == 3, "expected failed resource status");

    const auto after = repository.integrity("migu", 60);
    require(after.singers_without_songs >= before.singers_without_songs + 1, "integrity singers_without_songs did not increase");
    require(after.singers_without_albums >= before.singers_without_albums + 1, "integrity singers_without_albums did not increase");
    require(after.albums_without_details >= before.albums_without_details + 1, "integrity albums_without_details did not increase");
    require(after.songs_without_resource_tasks >= before.songs_without_resource_tasks + 1, "integrity songs_without_resource_tasks did not increase");
    require(after.resources_failed >= before.resources_failed + 1, "integrity resources_failed did not increase");
    require(after.resources_running_stale >= before.resources_running_stale + 1, "integrity resources_running_stale did not increase");
}

// 验证 reset 治理 SQL 会清空对应同步状态。
void assertBackfillReset(mysql::MySqlMusicCatalogQueryRepository &repository, const CatalogFixture &fixture)
{
    model::MiguBackfillTargetQuery singer_reset;
    singer_reset.source_ids = {fixture.singer_source_id};
    singer_reset.statuses = {3};
    require(repository.resetSingerSongs(singer_reset) == 1, "resetSingerSongs should update one row");

    singer_reset.statuses = {4};
    require(repository.resetSingerAlbums(singer_reset) == 1, "resetSingerAlbums should update one row");

    const auto singer = repository.findSinger("migu", fixture.singer_source_id);
    require(singer.has_value(), "singer should still exist after reset");
    require(singer->songs_sync_status == 0, "singer songs status should reset to 0");
    require(singer->albums_sync_status == 0, "singer albums status should reset to 0");

    model::MiguBackfillTargetQuery album_reset;
    album_reset.source_ids = {fixture.album_source_id};
    album_reset.statuses = {3};
    require(repository.resetAlbumDetails(album_reset) == 1, "resetAlbumDetails should update one row");

    album_reset.statuses = {4};
    require(repository.resetAlbumSongs(album_reset) == 1, "resetAlbumSongs should update one row");

    const auto album = repository.findAlbum("migu", fixture.album_source_id);
    require(album.has_value(), "album should still exist after reset");
    require(album->detail_sync_status == 0, "album detail status should reset to 0");
    require(album->songs_sync_status == 0, "album songs status should reset to 0");
}

// 验证资源队列 retry/reset/recover-stale 只治理状态，不执行资源获取。
void assertResourceGovernance(mysql::MySqlMusicCatalogQueryRepository &repository, const CatalogFixture &fixture, const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    model::MiguResourceQueueQuery failed_query;
    failed_query.keyword = fixture.song_source_id;
    failed_query.statuses = {3};
    failed_query.resource_kind = "audio";
    failed_query.format_type = "HQ";
    auto failed_resources = repository.listResources(failed_query);
    require(failed_resources.total == 1, "expected one failed resource before retry");
    const auto failed_resource_id = failed_resources.items.front().resource_id;
    require(repository.findResource(failed_resource_id).has_value(), "findResource should find failed fixture");

    model::MiguResourceActionQuery retry_query;
    retry_query.resource_ids = {failed_resource_id};
    retry_query.statuses = {3};
    require(repository.retryResources(retry_query) == 1, "retryResources should requeue one failed resource");
    auto retried = repository.findResource(failed_resource_id);
    require(retried.has_value() && retried->status == 0, "retried resource should become pending");

    model::MiguResourceActionQuery reset_query;
    reset_query.resource_ids = {failed_resource_id};
    reset_query.statuses = {0};
    require(repository.resetResources(reset_query) == 1, "resetResources should reset one pending resource");
    auto reset = repository.findResource(failed_resource_id);
    require(reset.has_value() && reset->retry_count == 0, "reset resource should clear retry count");

    model::MiguResourceQueueQuery skipped_query;
    skipped_query.keyword = fixture.song_source_id;
    skipped_query.statuses = {4};
    skipped_query.resource_kind = "audio";
    skipped_query.format_type = "PQ";
    auto skipped_resources = repository.listResources(skipped_query);
    require(skipped_resources.total == 1, "expected one skipped resource before guarded governance");
    const auto skipped_resource_id = skipped_resources.items.front().resource_id;

    model::MiguResourceActionQuery skipped_retry_query;
    skipped_retry_query.resource_ids = {skipped_resource_id};
    skipped_retry_query.statuses = {4};
    require(repository.retryResources(skipped_retry_query) == 0, "retryResources should not requeue skipped resources");
    auto skipped_after_retry = repository.findResource(skipped_resource_id);
    require(skipped_after_retry.has_value() && skipped_after_retry->status == 4, "skipped resource should remain skipped after retry");

    model::MiguResourceActionQuery skipped_reset_query;
    skipped_reset_query.resource_ids = {skipped_resource_id};
    skipped_reset_query.statuses = {4};
    require(repository.resetResources(skipped_reset_query) == 0, "resetResources should not reset skipped resources");
    auto skipped_after_reset = repository.findResource(skipped_resource_id);
    require(skipped_after_reset.has_value() && skipped_after_reset->status == 4, "skipped resource should remain skipped after reset");

    mysql::MySqlSongResourceQueue resource_queue(pool);
    model::SongResourceClaimQuery skipped_claim_query;
    skipped_claim_query.source = "migu";
    skipped_claim_query.resource_kind = "audio";
    skipped_claim_query.source_content_id = fixture.content_id;
    skipped_claim_query.statuses = {4};
    skipped_claim_query.limit = 10;
    require(resource_queue.claimPending(std::move(skipped_claim_query)).empty(), "claimPending should not claim skipped resources");

    model::MiguResourceActionQuery recover_query;
    recover_query.keyword = fixture.song_source_id;
    recover_query.resource_kind = "audio";
    recover_query.format_type = "SQ";
    recover_query.limit = 5;
    recover_query.stale_after_seconds = 60;
    require(repository.recoverStaleResources(recover_query, "fixture stale recovered") == 1, "recoverStaleResources should recover one stale running resource");
}

} // namespace

int main()
{
    try {
        auto pool = support::makeInitializedPool();
        FixtureGuard guard(pool);
        support::runOnExecutor(pool->executor(), cleanupCatalogFixtures(pool), "initial catalog fixture cleanup");

        mysql::MySqlMusicCatalogQueryRepository repository(pool);
        const auto before = repository.integrity("migu", 60);
        const auto fixture = support::runOnExecutor(pool->executor(), seedCatalogFixture(pool), "seed catalog fixture");

        assertCatalogQueries(repository, fixture, before);
        assertResourceGovernance(repository, fixture, pool);
        assertBackfillReset(repository, fixture);

        std::cout << "MiguCatalogQueryIntegrationTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguCatalogQueryIntegrationTest failed: " << e.what() << std::endl;
        return 1;
    }
}
