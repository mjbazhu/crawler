#include "migu/support/MySqlTestSupport.h"

#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/application/model/MusicCatalog.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/application/use_cases/CatalogMetadataCompletion.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>
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
    std::string suffix;                      // 唯一后缀
    std::string singer_source_id;            // 平台歌手 ID
    std::string album_source_id;             // 平台专辑 ID
    std::string song_source_id;              // 第一个平台歌曲 ID
    std::string content_id;                  // 第一个平台内容 ID
    std::string summarized_singer_source_id; // 真实汇总修正歌手 ID
    std::string summarized_album_source_id;  // 真实汇总修正专辑 ID
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
    co_await executeSql(pool, "DELETE FROM governance_events WHERE source = 'migu' AND action LIKE 'codex.%'");
    co_await executeSql(pool, "DELETE FROM songs WHERE normalized_title LIKE 'codex_catalog_%'");
    co_await executeSql(pool, "DELETE FROM albums WHERE normalized_title LIKE 'codex_catalog_%'");
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
    fixture.summarized_singer_source_id = "920" + fixture.suffix;
    fixture.summarized_album_source_id = "820" + fixture.suffix;

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
        "INSERT INTO singer_source_refs (singer_db_id, source, source_singer_id, source_gender, source_name_pinyin, source_country, source_song_total, source_album_total, songs_sync_status, songs_sync_error, albums_sync_status, albums_sync_error, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', '" + fixture.singer_source_id + "', 1, 'codex', 'huayu', 2, 1, 3, 'fixture songs failed', 4, 'fixture albums partial', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO singer_index_memberships(singer_source_ref_id,tab,source_tag) SELECT id,'fixture','test' FROM singer_catalog_sources WHERE source='migu' AND source_singer_id='" + fixture.singer_source_id + "' ON DUPLICATE KEY UPDATE source_tag=VALUES(source_tag)");

    const std::string summarized_singer_name = "Codex Catalog Summary Singer " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + summarized_singer_name + "', 'codex_catalog_summary_singer_" + fixture.suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto summarized_singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_singer_id, source_gender, source_name_pinyin, source_country, source_song_total, source_album_total, songs_sync_status, albums_sync_status, created_at, updated_at) VALUES (" + std::to_string(summarized_singer_db_id) + ", 'migu', '" + fixture.summarized_singer_source_id + "', 1, 'codex', 'huayu', 1, 1, 0, 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO singer_index_memberships(singer_source_ref_id,tab,source_tag) SELECT id,'fixture','test' FROM singer_catalog_sources WHERE source='migu' AND source_singer_id='" + fixture.summarized_singer_source_id + "' ON DUPLICATE KEY UPDATE source_tag=VALUES(source_tag)");

    const std::string prefix_singer_name = "Codex Catalog Prefix Singer " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + prefix_singer_name + "', 'codex_catalog_prefix_singer_" + fixture.suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto prefix_singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_singer_id, source_gender, source_name_pinyin, source_country, songs_sync_status, albums_sync_status, created_at, updated_at) VALUES (" + std::to_string(prefix_singer_db_id) + ", 'migu', '" + fixture.singer_source_id + "99', 1, 'codex', 'huayu', 0, 0, " + std::to_string(now + 1) + ", " + std::to_string(now + 1) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO singer_index_memberships(singer_source_ref_id,tab,source_tag) SELECT id,'fixture','test' FROM singer_catalog_sources WHERE source='migu' AND source_singer_id='" + fixture.singer_source_id + "99' ON DUPLICATE KEY UPDATE source_tag=VALUES(source_tag)");

    const std::string album_title = "Codex Catalog Album " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, release_date, identity_hash, created_at, updated_at) VALUES ('" + album_title + "', 'codex_catalog_album_" + fixture.suffix + "', '2026-01-01', '" + identityHash(fixture.suffix) + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_album_id, source_resource_type, source_album_kind, detail_sync_status, detail_sync_error, songs_sync_status, songs_sync_count, songs_sync_error, created_at, updated_at, source_song_total, source_title_pinyin, summary) VALUES (" + std::to_string(album_db_id) + ", 'migu', '" + fixture.album_source_id + "', '5', 2, 3, 'fixture detail failed', 4, 1, 'fixture songs partial', " + std::to_string(now) + ", " + std::to_string(now) + ", '2', 'catalog-pinyin', 'catalog-summary')",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO album_artist_refs(album_source_ref_id,singer_source_ref_id,artist_role,artist_order,is_primary) SELECT a.id,s.id,'artist',1,1 FROM album_catalog_sources a JOIN singer_catalog_sources s ON s.source=a.source AND s.source_singer_id='" + fixture.singer_source_id + "' WHERE a.source='migu' AND a.source_album_id='" + fixture.album_source_id + "' ON DUPLICATE KEY UPDATE is_primary=1");

    co_await pool->coExecute("INSERT INTO migu_dalbum_details(album_source_ref_id,source_material_id) SELECT id,'" + fixture.album_source_id + "_material' FROM album_source_refs WHERE source='migu' AND source_album_id='" + fixture.album_source_id + "'");
    const std::string summarized_album_title = "Codex Catalog Summary Album " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, release_date, identity_hash, created_at, updated_at) VALUES ('" + summarized_album_title + "', 'codex_catalog_summary_album_" + fixture.suffix + "', '2026-01-01', '" + identityHash(+"summary_" + fixture.suffix) + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto summarized_album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_album_id, source_resource_type, source_album_kind, detail_sync_status, songs_sync_status, created_at, updated_at) VALUES (" + std::to_string(summarized_album_db_id) + ", 'migu', '" + fixture.summarized_album_source_id + "', '5', 2, 2, 2, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO album_artist_refs(album_source_ref_id,singer_source_ref_id,artist_role,artist_order,is_primary) SELECT a.id,s.id,'artist',1,1 FROM album_catalog_sources a JOIN singer_catalog_sources s ON s.source=a.source AND s.source_singer_id='" + fixture.summarized_singer_source_id + "' WHERE a.source='migu' AND a.source_album_id='" + fixture.summarized_album_source_id + "' ON DUPLICATE KEY UPDATE is_primary=1");

    const std::string song_title = "Codex Catalog Song " + fixture.suffix;
    co_await connection->async_execute(
        "INSERT INTO songs (title, normalized_title, created_at, updated_at) VALUES ('" + song_title + "', 'codex_catalog_song_" + fixture.suffix + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_source_refs (song_db_id, source, source_song_id, source_content_id, source_resource_type, source_title_pinyin, source_duration_seconds, copyright_id, metadata_status, created_at, updated_at) VALUES (" + std::to_string(song_db_id) + ", 'migu', '" + fixture.song_source_id + "', '" + fixture.content_id + "', '2', 'codexcatalogsong', 180, 'copyright-" + fixture.suffix + "', 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_ref_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", (SELECT id FROM singer_catalog_sources WHERE source = 'migu' AND source_singer_id = '" + fixture.singer_source_id + "' LIMIT 1), 'artist', 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", (SELECT id FROM singer_catalog_sources WHERE source = 'migu' AND source_singer_id = '" + fixture.summarized_singer_source_id + "' LIMIT 1), 'artist', 2, 0, 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO album_song_refs (album_source_ref_id, song_source_ref_id, disc_no, track_no, song_order, created_at, updated_at) VALUES ((SELECT id FROM album_catalog_sources WHERE source = 'migu' AND source_album_id = '" + fixture.album_source_id + "' LIMIT 1), " + std::to_string(song_ref_id) + ", 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO song_source_audio_formats (song_source_ref_id, format_type, resource_type, iformat, aformat, isize, asize, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", 'HQ', '2', '020010', '020010', 123, 123, " + std::to_string(now) + ", " + std::to_string(now) + "), (" + std::to_string(song_ref_id) + ", 'SQ', '2', '011003', '011002', 456, 456, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO song_resources (song_source_ref_id, resource_kind, format_type, local_path, size_bytes, checksum, status, retry_count, last_error, error_category, result_code, fetched_at, created_at, updated_at) VALUES (" + std::to_string(song_ref_id) + ", 'audio', 'HQ', '', 0, '', 3, 2, 'fixture failed', 'network', 'network_failed', 0, " + std::to_string(now) + ", " + std::to_string(now) + "), (" + std::to_string(song_ref_id) + ", 'audio', 'SQ', '', 0, '', 1, 1, '', NULL, NULL, 0, " + std::to_string(now) + ", " + std::to_string(now - 7200) + "), (" + std::to_string(song_ref_id) + ", 'audio', 'PQ', '', 0, '', 4, 0, 'fixture skipped', 'resource', 'digital_album', 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
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
        "INSERT INTO song_source_refs (song_db_id, source, source_song_id, source_content_id, source_resource_type, source_title_pinyin, source_duration_seconds, metadata_status, created_at, updated_at) VALUES (" + std::to_string(song_two_db_id) + ", 'migu', '" + song_two_id + "', '" + content_two_id + "', '2', 'codexcatalogsongmissing', 181, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_two_ref_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);

    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_two_ref_id) + ", (SELECT id FROM singer_catalog_sources WHERE source = 'migu' AND source_singer_id = '" + fixture.singer_source_id + "' LIMIT 1), 'artist', 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_await connection->async_execute(
        "INSERT INTO album_song_refs (album_source_ref_id, song_source_ref_id, disc_no, track_no, song_order, created_at, updated_at) VALUES ((SELECT id FROM album_catalog_sources WHERE source = 'migu' AND source_album_id = '" + fixture.album_source_id + "' LIMIT 1), " + std::to_string(song_two_ref_id) + ", 1, 2, 2, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    co_return fixture;
}

// 验证列表、详情、资源队列和 integrity 聚合。
void assertCatalogQueries(mysql::MySqlMusicCatalogQueryRepository &repository, const CatalogFixture &fixture, const model::MiguIntegritySummary &before, const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    model::MiguSingerCatalogQuery singer_query;
    singer_query.source_singer_id = fixture.singer_source_id;
    auto singers = repository.listSingers(singer_query);
    require(singers.total == 1, "expected one singer fixture");
    require(singers.items.front().source_singer_id == fixture.singer_source_id, "unexpected singer source id");
    require(singers.items.front().songs_sync_count == 2, "singer songs summary should use song_artist_refs");
    require(singers.items.front().albums_sync_count == 1, "singer albums summary should use album_source_refs");
    require(repository.findSinger("migu", fixture.singer_source_id).has_value(), "findSinger should find fixture");

    model::MiguSingerCatalogQuery singer_priority_query;
    singer_priority_query.keyword = fixture.singer_source_id;
    auto prioritized_singers = repository.listSingers(singer_priority_query);
    require(prioritized_singers.total >= 2, "fuzzy singer ID query should include exact and prefix matches");
    require(!prioritized_singers.items.empty() && prioritized_singers.items.front().source_singer_id == fixture.singer_source_id, "exact source_singer_id match should be ranked first");

    model::MiguSingerCatalogQuery singer_count_query;
    singer_count_query.keyword = fixture.singer_source_id;
    singer_count_query.min_songs_count = 2;
    singer_count_query.max_albums_count = 1;
    require(repository.listSingers(singer_count_query).total == 1, "singer count filters should include matching fixture");
    singer_count_query.min_albums_count = 2;
    require(repository.listSingers(singer_count_query).total == 0, "singer count filters should exclude non-matching fixture");

    auto summarized = repository.findSinger("migu", fixture.summarized_singer_source_id);
    require(summarized.has_value(), "findSinger should find summarized fixture");
    require(summarized->songs_sync_count == 1, "summarized singer songs count should be derived from real refs");
    require(summarized->albums_sync_count == 1, "summarized singer albums count should be derived from real refs");
    require(summarized->songs_sync_status == 0 && summarized->songs_synced_at == 0, "song relations must not fabricate directory completion or sync time");
    require(summarized->albums_sync_status == 0 && summarized->albums_synced_at == 0, "album relations must not fabricate directory completion or sync time");

    model::MiguSingerCatalogQuery uncollected_singer_query;
    uncollected_singer_query.source_singer_id = fixture.summarized_singer_source_id;
    const auto uncollected_singer = repository.listSingers(uncollected_singer_query);
    require(uncollected_singer.total == 1 && uncollected_singer.items.front().songs_sync_status == 0 && uncollected_singer.items.front().albums_sync_status == 0, "unfiltered catalog list overwrote persisted directory states");

    model::MiguSingerCatalogQuery pending_singer_query;
    pending_singer_query.keyword = fixture.summarized_singer_source_id;
    pending_singer_query.songs_sync_statuses = {0, 3, 4};
    require(repository.listSingers(pending_singer_query).total == 1, "uncollected singer must remain eligible despite associated songs");
    pending_singer_query.albums_sync_statuses = {0};
    require(repository.listSingers(pending_singer_query).total == 1, "filtered catalog list overwrote persisted album directory state");

    model::MiguAlbumCatalogQuery album_query;
    album_query.keyword = fixture.album_source_id;
    album_query.album_kind = 2;
    album_query.detail_sync_statuses = {3};
    auto albums = repository.listAlbums(album_query);
    require(albums.total == 1, "expected one album fixture");
    require(albums.items.front().source_album_id == fixture.album_source_id, "unexpected album source id");
    require(albums.items.front().album_songs_count == 2, "album relation count should use album_song_refs");
    require(albums.items.front().songs_sync_count == 1, "album sync count must preserve the latest collection result");
    require(albums.items.front().songs_sync_status == 4, "existing relations must not replace album collection status");
    require(albums.items.front().source_title == albums.items.front().title, "album source title must be derived from albums");
    require(albums.items.front().source_item_key == "album:" + fixture.album_source_id, "album source key must be derived from its identity");
    auto album_detail = repository.findAlbum("migu", fixture.album_source_id);
    require(album_detail.has_value(), "findAlbum should find fixture");
    require(album_detail->source_title_pinyin == "catalog-pinyin" && album_detail->summary == "catalog-summary", "album metadata fields must be returned by the catalog query");
    // 不同来源复用名称池时，重复采集不能因大小写及并发顺序改变显示名称。
    model::SingerSourceMetadata upper;
    upper.source = "migu";
    upper.source_singer_id = fixture.singer_source_id + "_case_a";
    upper.name = "RAY";
    upper.normalized_name = "codex_catalog_stable_" + fixture.suffix;
    auto lower = upper;
    lower.source_singer_id = fixture.singer_source_id + "_case_b";
    lower.name = "Ray";
    model::MetadataWriteBatch names;
    names.singers = {upper, lower};
    mysql::MySqlMetadataBatchWriter name_writer(pool);
    require(name_writer.saveBatch(names).isSuccess(), "name pool seed failed");
    names.singers = {lower, upper};
    require(name_writer.saveBatch(names).isSuccess(), "name pool replay failed");
    const auto stable_name = repository.findSinger("migu", lower.source_singer_id);
    require(stable_name && stable_name->name == "RAY", "same-name display changed on replay");
    support::runOnExecutor(pool->executor(), pool->coExecute("DELETE FROM singers WHERE normalized_name='" + upper.normalized_name + "'"), "cleanup name pool fixture");
    const auto material_alias = repository.findAlbum("migu", fixture.album_source_id + "_material");
    require(material_alias && material_alias->source_ref_id == album_detail->source_ref_id, "material alias must reuse the existing album instead of refetching detail");
    // 缓存为空时也复用已存详情；列表资料不能降级字段或冒充一次新同步。
    support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=2 WHERE source='migu' AND source_album_id='" + fixture.album_source_id + "'"), "complete album fixture");
    crawler::application::use_cases::CatalogMetadataCompletion completion({nullptr, nullptr, std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool), nullptr});
    model::MetadataWriteBatch sparse;
    model::AlbumSourceMetadata sparse_album;
    sparse_album.source = "migu";
    sparse_album.source_album_id = fixture.album_source_id + "_material";
    sparse_album.title = "sparse list title";
    sparse.albums = {sparse_album};
    require(!completion.completeAlbums(sparse).isSuccess(), "partial tracks were incorrectly treated as a complete cached album");
    support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET songs_sync_status=2,songs_sync_count=2 WHERE source='migu' AND source_album_id='" + fixture.album_source_id + "'"), "complete album tracks fixture");
    const auto reused = completion.completeAlbums(sparse);
    require(reused.isSuccess() && reused.data()->albums.size() == 1, "cold completion should reuse the canonical album");
    require(reused.data()->albums.front().summary == "catalog-summary" && reused.data()->albums.front().detail_sync_status == 0, "reused metadata must retain detail fields without advancing sync time");
    support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=3,songs_sync_status=4,songs_sync_count=1 WHERE source='migu' AND source_album_id='" + fixture.album_source_id + "'"), "restore album fixture");
    require(album_detail->album_songs_count == 2, "album detail relation count should use album_song_refs");
    require(album_detail->songs_sync_count == 1, "album detail must preserve the latest sync count");

    // 接口同时暴露事实关系数和最近一次同步数量，完成状态不由关系数推断。
    crawler::application::use_cases::CrawlerServiceDependencies dependencies;
    dependencies.persistence.catalog_query_repository = std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool);
    auto service = std::make_shared<crawler::application::use_cases::CrawlerService>(std::move(dependencies));
    crawler::api::controllers::CrawlerController controller(service);
    crawler::api::http::HttpRequest request;
    request.method = "GET";
    request.path = "/api/migu/albums";
    request.query_params["q"] = fixture.album_source_id;
    const auto response = controller.handleListMiguAlbums(std::move(request));
    require(response.status_code == 200, "album API failed");
    const auto payload = nlohmann::json::parse(response.body);
    const auto &item = payload.at("items").at(0);
    require(item.at("album_songs_count") == 2 && item.at("songs_count") == 2, "album API must return current relationship counts");
    require(item.at("songs_sync_count") == 1 && item.at("songs_sync_status") == 4, "album API must preserve the latest collection result and state");

    model::MiguAlbumCatalogQuery album_count_query;
    album_count_query.keyword = fixture.album_source_id;
    album_count_query.min_songs_count = 2;
    require(repository.listAlbums(album_count_query).total == 1, "album count filters should include matching fixture");
    album_count_query.max_songs_count = 1;
    require(repository.listAlbums(album_count_query).total == 0, "album count filters should exclude non-matching fixture");

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
    require(after.singers_without_songs >= before.singers_without_songs + 1, "integrity singers_without_songs should include singer sources without real song refs");
    require(after.singers_without_albums >= before.singers_without_albums + 1, "integrity singers_without_albums should include singer sources without real album refs");
    require(after.albums_without_details >= before.albums_without_details + 1, "integrity albums_without_details did not increase");
    require(after.songs_without_resource_tasks >= before.songs_without_resource_tasks + 1, "integrity songs_without_resource_tasks did not increase");
    require(after.resources_failed >= before.resources_failed + 1, "integrity resources_failed did not increase");
    require(after.resources_running_stale >= before.resources_running_stale + 1, "integrity resources_running_stale did not increase");

    const auto overview = repository.overview();
    require(overview.singer_song_relations >= 3, "overview should include singer-song relation count");
    require(overview.singer_album_relations >= 2, "overview should include singer-album relation count");
    require(overview.album_song_relations >= 2, "overview should include album-song relation count");
}

// 验证 reset 治理 SQL 不会让已有真实关系的歌手重新暴露为缺内容。
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
    require(singer->songs_sync_status == 0, "song relations must not undo an explicit sync reset");
    require(singer->songs_sync_count == 2, "singer songs count should stay derived after reset");
    require(singer->albums_sync_status == 0, "album relations must not undo an explicit sync reset");
    require(singer->albums_sync_count == 1, "singer albums count should stay derived after reset");

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

// 创建用于资源 upsert 防回退验证的歌曲来源。
model::SongSourceMetadata makeUpsertSong(std::string suffix, bool with_audio_format)
{
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "codex_catalog_upsert_song_" + suffix + (with_audio_format ? "_audio" : "_placeholder");
    song.source_content_id = "codex_catalog_upsert_content_" + suffix + (with_audio_format ? "_audio" : "_placeholder");
    song.source_resource_type = "2";
    song.title = "Codex Catalog Upsert Song " + suffix + (with_audio_format ? " Audio" : " Placeholder");
    song.normalized_title = "codex_catalog_upsert_song_" + suffix + (with_audio_format ? "_audio" : "_placeholder");
    song.source_duration_seconds = 180;
    song.cover_url = "https://example.invalid/upsert.webp";
    song.lyric_url = "https://example.invalid/upsert.lrc";
    song.mrc_url = "https://example.invalid/upsert.mrc";
    song.trc_url = "https://example.invalid/upsert.trc";
    song.copyright_id = "codex-upsert-" + suffix;
    if (with_audio_format) {
        model::SongSourceAudioFormat format;
        format.format_type = "PQ";
        format.resource_type = "2";
        format.iformat = "020010";
        format.aformat = "020010";
        format.isize = 123;
        format.asize = 123;
        song.audio_formats.push_back(std::move(format));
    }
    return song;
}

// 查询单个资源当前状态。
int resourceStatus(mysql::MySqlMusicCatalogQueryRepository &repository, const std::string &source_content_id, const std::string &kind, const std::string &format_type)
{
    model::MiguResourceQueueQuery query;
    query.keyword = source_content_id;
    query.resource_kind = kind;
    query.format_type = format_type;
    auto resources = repository.listResources(query);
    require(resources.total == 1, "expected exactly one resource for " + kind + ":" + format_type);
    return resources.items.front().status;
}

// 验证歌曲重复入库不会把永久跳过的资源重新置为待处理。
void assertResourceUpsertPreservesSkipped(mysql::MySqlMusicCatalogQueryRepository &repository, const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const CatalogFixture &fixture)
{
    mysql::MySqlSongSourceRepository song_repository(pool);

    auto audio_song = makeUpsertSong(fixture.suffix, true);
    require(song_repository.saveBatch({audio_song}), "initial audio upsert song save should succeed");
    support::runOnExecutor(
        pool->executor(),
        executeSql(pool, "UPDATE song_resources sr JOIN song_catalog_sources r ON r.id = sr.song_source_ref_id SET sr.status = 4, sr.last_error = 'fixture skipped', sr.error_category = 'resource', sr.result_code = 'digital_album' WHERE r.source = 'migu' AND r.source_content_id = '" + audio_song.source_content_id + "' AND ((sr.resource_kind = 'audio' AND sr.format_type = 'PQ') OR (sr.resource_kind = 'lyric' AND sr.format_type = ''))"),
        "mark audio and lyric resources skipped"
    );

    require(song_repository.saveBatch({audio_song}), "second audio upsert song save should succeed");
    require(resourceStatus(repository, audio_song.source_content_id, "audio", "PQ") == 4, "audio format upsert should preserve skipped status");
    require(resourceStatus(repository, audio_song.source_content_id, "lyric", "") == 4, "text resource upsert should preserve skipped status");

    auto placeholder_song = makeUpsertSong(fixture.suffix, false);
    require(song_repository.saveBatch({placeholder_song}), "initial placeholder song save should succeed");
    support::runOnExecutor(
        pool->executor(),
        executeSql(pool, "UPDATE song_resources sr JOIN song_catalog_sources r ON r.id = sr.song_source_ref_id SET sr.status = 4, sr.last_error = 'fixture skipped', sr.error_category = 'resource', sr.result_code = 'digital_album' WHERE r.source = 'migu' AND r.source_content_id = '" + placeholder_song.source_content_id + "' AND sr.resource_kind = 'audio' AND sr.format_type = ''"),
        "mark placeholder resource skipped"
    );

    require(song_repository.saveBatch({placeholder_song}), "second placeholder song save should succeed");
    require(resourceStatus(repository, placeholder_song.source_content_id, "audio", "") == 4, "audio placeholder upsert should preserve skipped status");
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

// 验证治理事件展示字段支持历史回填、快照优先和目标缺失兼容。
void assertGovernanceEventTargets(mysql::MySqlMusicCatalogQueryRepository &repository, const CatalogFixture &fixture)
{
    model::MiguResourceQueueQuery failed_query;
    failed_query.keyword = fixture.song_source_id;
    failed_query.statuses = {3};
    failed_query.resource_kind = "audio";
    failed_query.format_type = "HQ";
    auto failed_resources = repository.listResources(failed_query);
    require(failed_resources.total == 1, "expected one failed resource for governance event fixture");
    const auto resource = failed_resources.items.front();

    model::MiguGovernanceEventRecord old_event;
    old_event.action = "codex.resource.retry";
    old_event.target_type = "resource";
    old_event.target_id = std::to_string(resource.resource_id);
    old_event.before_status = "3";
    old_event.after_status = "0";
    old_event.result_code = "ok";
    old_event.payload = "{}";

    model::MiguGovernanceEventRecord snapshot_event;
    snapshot_event.action = "codex.resource.snapshot";
    snapshot_event.target_type = "resource";
    snapshot_event.target_id = "0";
    snapshot_event.target_name = "Snapshot Song";
    snapshot_event.target_summary = "Snapshot Summary";
    snapshot_event.target_source_id = "snapshot-source-id";
    snapshot_event.target_owner_name = "Snapshot Owner";
    snapshot_event.result_code = "ok";
    snapshot_event.payload = "{}";

    model::MiguGovernanceEventRecord missing_event;
    missing_event.action = "codex.resource.missing";
    missing_event.target_type = "resource";
    missing_event.target_id = "999999999999";
    missing_event.result_code = "failed";
    missing_event.reason = "not_found";
    missing_event.payload = "{}";

    repository.appendGovernanceEvents({old_event, snapshot_event, missing_event});

    model::MiguGovernanceEventQuery old_query;
    old_query.action = "codex.resource.retry";
    auto old_page = repository.listGovernanceEvents(old_query);
    require(old_page.total == 1 && old_page.items.size() == 1, "old governance event should be listed");
    const auto &old_item = old_page.items.front();
    require(old_item.target_name == resource.title, "old governance event should backfill resource title");
    require(old_item.target_summary.find("音频") != std::string::npos, "old governance event should include resource kind label");
    require(old_item.target_summary.find("HQ") != std::string::npos, "old governance event should include format type");
    require(old_item.target_source_id == fixture.content_id, "old governance event should backfill source content id");
    require(old_item.target_owner_name == resource.artist, "old governance event should backfill artist name");

    model::MiguGovernanceEventQuery snapshot_query;
    snapshot_query.action = "codex.resource.snapshot";
    auto snapshot_page = repository.listGovernanceEvents(snapshot_query);
    require(snapshot_page.total == 1 && snapshot_page.items.size() == 1, "snapshot governance event should be listed");
    const auto &snapshot_item = snapshot_page.items.front();
    require(snapshot_item.target_name == "Snapshot Song", "snapshot target_name should win over joins");
    require(snapshot_item.target_summary == "Snapshot Summary", "snapshot target_summary should win over joins");
    require(snapshot_item.target_source_id == "snapshot-source-id", "snapshot target_source_id should win over joins");
    require(snapshot_item.target_owner_name == "Snapshot Owner", "snapshot target_owner_name should win over joins");

    model::MiguGovernanceEventQuery missing_query;
    missing_query.action = "codex.resource.missing";
    auto missing_page = repository.listGovernanceEvents(missing_query);
    require(missing_page.total == 1 && missing_page.items.size() == 1, "missing target governance event should still be listed");
    const auto &missing_item = missing_page.items.front();
    require(missing_item.target_name.empty(), "missing target event should keep empty target_name");
    require(missing_item.reason == "not_found", "missing target event should preserve business reason");
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

        assertCatalogQueries(repository, fixture, before, pool);
        assertGovernanceEventTargets(repository, fixture);
        assertResourceGovernance(repository, fixture, pool);
        assertResourceUpsertPreservesSkipped(repository, pool, fixture);
        assertBackfillReset(repository, fixture);

        std::cout << "MiguCatalogQueryIntegrationTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguCatalogQueryIntegrationTest failed: " << e.what() << std::endl;
        return 1;
    }
}
