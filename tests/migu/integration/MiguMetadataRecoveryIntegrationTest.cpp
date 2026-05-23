#include "migu/support/MySqlTestSupport.h"

#include "crawler/application/model/CrawlBatch.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
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
#include <vector>

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

// stale recovery 测试 fixture 的平台 ID。
struct RecoveryFixture {
    std::string suffix;                // 唯一后缀
    std::string stale_songs_singer;    // 歌曲同步 stale running 歌手
    std::string fresh_songs_singer;    // 歌曲同步未超时 running 歌手
    std::string stale_albums_singer;   // 专辑同步 stale running 歌手
    std::string stale_detail_album;    // 详情同步 stale running 专辑
    std::string fresh_detail_album;    // 详情同步未超时 running 专辑
    std::string stale_songs_album;     // 专辑歌曲 stale running 专辑
    std::string batch_id;              // stale running 批次
};

// 生成跨测试运行唯一后缀。
std::string uniqueSuffix()
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<long long>(ticks));
}

// 为专辑 identity_hash 生成 64 字符唯一值。
std::string identityHash(const std::string &suffix, const std::string &tag)
{
    std::string value = "codexrecovery" + tag + suffix;
    value.resize(64, '0');
    return value;
}

// 执行 SQL 并忽略结果。
boost::asio::awaitable<void> executeSql(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    (void)co_await pool->coExecute(std::move(sql));
}

// 清理 stale recovery 测试 fixture。
boost::asio::awaitable<void> cleanupRecoveryFixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    co_await executeSql(pool, "DELETE FROM crawl_batches WHERE batch_id LIKE 'codex_recovery_%'");
    co_await executeSql(pool, "DELETE FROM album_source_refs WHERE source = 'migu' AND source_item_key LIKE 'codex_recovery_%'");
    co_await executeSql(pool, "DELETE FROM albums WHERE normalized_title LIKE 'codex_recovery_%'");
    co_await executeSql(pool, "DELETE FROM singer_source_refs WHERE source = 'migu' AND source_item_key LIKE 'codex_recovery_%'");
    co_await executeSql(pool, "DELETE FROM singers WHERE normalized_name LIKE 'codex_recovery_%'");
}

// 在测试结束时清理 fixture，避免污染本地库。
class FixtureGuard {
public:
    // 保存连接池，析构时清理测试数据。
    explicit FixtureGuard(std::shared_ptr<mysql::MySqlConnectionPool> pool) : pool_(std::move(pool))
    {}

    // 析构清理不向外抛异常。
    ~FixtureGuard()
    {
        try {
            if (pool_) {
                support::runOnExecutor(pool_->executor(), cleanupRecoveryFixtures(pool_), "cleanup recovery fixtures");
            }
        } catch (...) {
        }
    }

private:
    std::shared_ptr<mysql::MySqlConnectionPool> pool_; // 测试连接池
};

// 插入一个歌手来源。
boost::asio::awaitable<void> seedSinger(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &suffix,
    const std::string &tag,
    const std::string &source_singer_id,
    int songs_status,
    int albums_status,
    std::uint64_t updated_at
)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string name = "Codex Recovery Singer " + tag + " " + suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + name + "', 'codex_recovery_singer_" + tag + "_" + suffix + "', 1, 1, " + std::to_string(updated_at) + ", " + std::to_string(updated_at) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_item_key, source_singer_id, source_name_pinyin, source_country, songs_sync_status, songs_sync_error, songs_claim_token, songs_claimed_at, songs_claim_owner, albums_sync_status, albums_sync_error, albums_claim_token, albums_claimed_at, albums_claim_owner, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', 'codex_recovery_singer:" + tag + ":" + suffix + "', '" + source_singer_id + "', 'codex', 'huayu', " + std::to_string(songs_status) + ", 'seed songs running', 'seed-songs-token', " + std::to_string(updated_at) + ", 'test', " + std::to_string(albums_status) + ", 'seed albums running', 'seed-albums-token', " + std::to_string(updated_at) + ", 'test', " + std::to_string(updated_at) + ", " + std::to_string(updated_at) + ")",
        result,
        boost::asio::use_awaitable
    );
}

// 插入一个专辑来源。
boost::asio::awaitable<void> seedAlbum(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &suffix,
    const std::string &tag,
    const std::string &source_album_id,
    int detail_status,
    int songs_status,
    std::uint64_t updated_at
)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string title = "Codex Recovery Album " + tag + " " + suffix;
    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, primary_artist_name, release_date, identity_hash, created_at, updated_at) VALUES ('" + title + "', 'codex_recovery_album_" + tag + "_" + suffix + "', 'Codex Recovery Artist', '2026-01-01', '" + identityHash(suffix, tag) + "', " + std::to_string(updated_at) + ", " + std::to_string(updated_at) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_item_key, source_album_id, source_resource_type, source_res_type, source_album_kind, source_title, source_artist_id, source_artist_name, detail_sync_status, detail_sync_error, detail_claim_token, detail_claimed_at, detail_claim_owner, songs_sync_status, songs_sync_error, songs_claim_token, songs_claimed_at, songs_claim_owner, created_at, updated_at) VALUES (" + std::to_string(album_db_id) + ", 'migu', 'codex_recovery_album:" + tag + ":" + suffix + "', '" + source_album_id + "', '5', '1', 2, '" + title + "', 'recovery-artist', 'Codex Recovery Artist', " + std::to_string(detail_status) + ", 'seed detail running', 'seed-detail-token', " + std::to_string(updated_at) + ", 'test', " + std::to_string(songs_status) + ", 'seed songs running', 'seed-album-songs-token', " + std::to_string(updated_at) + ", 'test', " + std::to_string(updated_at) + ", " + std::to_string(updated_at) + ")",
        result,
        boost::asio::use_awaitable
    );
}

// 插入完整 stale recovery 测试数据。
boost::asio::awaitable<RecoveryFixture> seedRecoveryFixture(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    RecoveryFixture fixture;
    fixture.suffix = uniqueSuffix();
    fixture.stale_songs_singer = "970" + fixture.suffix + "1";
    fixture.fresh_songs_singer = "970" + fixture.suffix + "2";
    fixture.stale_albums_singer = "970" + fixture.suffix + "3";
    fixture.stale_detail_album = "870" + fixture.suffix + "1";
    fixture.fresh_detail_album = "870" + fixture.suffix + "2";
    fixture.stale_songs_album = "870" + fixture.suffix + "3";
    fixture.batch_id = "codex_recovery_batch_" + fixture.suffix;

    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const auto stale = now - 7200;
    co_await seedSinger(pool, fixture.suffix, "stale_songs", fixture.stale_songs_singer, 1, 2, stale);
    co_await seedSinger(pool, fixture.suffix, "fresh_songs", fixture.fresh_songs_singer, 1, 2, now);
    co_await seedSinger(pool, fixture.suffix, "stale_albums", fixture.stale_albums_singer, 2, 1, stale);
    co_await seedAlbum(pool, fixture.suffix, "stale_detail", fixture.stale_detail_album, 1, 0, stale);
    co_await seedAlbum(pool, fixture.suffix, "fresh_detail", fixture.fresh_detail_album, 1, 0, now);
    co_await seedAlbum(pool, fixture.suffix, "stale_songs", fixture.stale_songs_album, 2, 1, stale);
    co_return fixture;
}

// 创建一个超时运行批次和两个未完成 item。
void seedRunningBatch(mysql::MySqlBatchTaskRepository &repository, const RecoveryFixture &fixture)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    const auto stale = now - 7200;
    model::CrawlBatchRecord batch;
    batch.batch_id = fixture.batch_id;
    batch.source = "migu";
    batch.task_type = "albumDetails";
    batch.state = "Running";
    batch.request_params = "{}";
    batch.settings_snapshot = "{}";
    batch.limit_count = 50;
    batch.target_count = 2;
    batch.processed_count = 0;
    batch.success_count = 0;
    batch.failure_count = 0;
    batch.created_at = stale;
    batch.started_at = stale;
    batch.updated_at = stale;

    std::vector<model::CrawlBatchItemRecord> items;
    for (int index = 0; index < 2; ++index) {
        model::CrawlBatchItemRecord item;
        item.batch_id = fixture.batch_id;
        item.item_index = index;
        item.item_key = "album:" + fixture.stale_detail_album + ":" + std::to_string(index);
        item.item_type = "albumDetail";
        item.item_label = fixture.stale_detail_album;
        item.source = "migu";
        item.source_album_id = fixture.stale_detail_album;
        item.state = index == 0 ? "Pending" : "Running";
        item.payload = "{}";
        item.created_at = stale;
        item.updated_at = stale;
        items.push_back(std::move(item));
    }
    require(repository.createBatch(batch, items), "create stale running batch should succeed");
}

// 验证元数据 stale running 恢复只影响超时记录。
void assertMetadataRecovery(mysql::MySqlMusicCatalogQueryRepository &repository, const RecoveryFixture &fixture)
{
    const std::string error = "stale running recovered";
    require(repository.recoverStaleSingerSongs("migu", 3600, error) == 1, "recoverStaleSingerSongs should recover one row");
    require(repository.recoverStaleSingerAlbums("migu", 3600, error) == 1, "recoverStaleSingerAlbums should recover one row");
    require(repository.recoverStaleAlbumDetails("migu", 3600, error) == 1, "recoverStaleAlbumDetails should recover one row");
    require(repository.recoverStaleAlbumSongs("migu", 3600, error) == 1, "recoverStaleAlbumSongs should recover one row");

    const auto stale_songs = repository.findSinger("migu", fixture.stale_songs_singer);
    const auto fresh_songs = repository.findSinger("migu", fixture.fresh_songs_singer);
    const auto stale_albums = repository.findSinger("migu", fixture.stale_albums_singer);
    require(stale_songs && stale_songs->songs_sync_status == 3, "stale singer songs should become failed");
    require(stale_songs->songs_sync_error == error, "stale singer songs should record recovery error");
    require(fresh_songs && fresh_songs->songs_sync_status == 1, "fresh singer songs should remain running");
    require(stale_albums && stale_albums->albums_sync_status == 3, "stale singer albums should become failed");

    const auto stale_detail = repository.findAlbum("migu", fixture.stale_detail_album);
    const auto fresh_detail = repository.findAlbum("migu", fixture.fresh_detail_album);
    const auto stale_album_songs = repository.findAlbum("migu", fixture.stale_songs_album);
    require(stale_detail && stale_detail->detail_sync_status == 3, "stale album detail should become failed");
    require(stale_detail->detail_sync_error == error, "stale album detail should record recovery error");
    require(fresh_detail && fresh_detail->detail_sync_status == 1, "fresh album detail should remain running");
    require(stale_album_songs && stale_album_songs->songs_sync_status == 3, "stale album songs should become failed");
}

// 验证运行中批次超时恢复后 batch、item、event 状态一致。
void assertBatchRecovery(mysql::MySqlBatchTaskRepository &repository, const RecoveryFixture &fixture)
{
    const std::string error = "stale running recovered";
    require(repository.recoverStaleRunningBatches("migu", 3600, error) == 1, "recoverStaleRunningBatches should recover one batch");
    const auto batch = repository.findBatch(fixture.batch_id);
    require(batch && batch->state == "Failed", "stale running batch should become Failed");
    require(batch->last_error == error, "stale batch should record recovery error");
    require(batch->failure_count == 2, "stale batch failure_count should cover unfinished items");

    model::CrawlBatchItemQuery item_query;
    item_query.page_size = 10;
    const auto items = repository.listItems(fixture.batch_id, item_query);
    require(items.total == 2, "expected two recovered batch items");
    for (const auto &item : items.items) {
        require(item.state == "Failed", "recovered item should become Failed");
        require(item.error_category == "timeout", "recovered item should have timeout category");
        require(item.result_code == "timeout", "recovered item should have timeout result code");
    }

    model::CrawlBatchEventQuery event_query;
    event_query.page_size = 10;
    const auto events = repository.listEvents(fixture.batch_id, event_query);
    bool found_timeout_recovered = false;
    for (const auto &event : events.items) {
        if (event.event_type == "timeoutRecovered") {
            found_timeout_recovered = true;
        }
    }
    require(found_timeout_recovered, "timeoutRecovered event should be appended");
}

} // namespace

int main()
{
    try {
        auto pool = support::makeInitializedPool();
        FixtureGuard guard(pool);
        support::runOnExecutor(pool->executor(), cleanupRecoveryFixtures(pool), "initial recovery fixture cleanup");

        mysql::MySqlMusicCatalogQueryRepository catalog_repository(pool);
        mysql::MySqlBatchTaskRepository batch_repository(pool);
        const auto fixture = support::runOnExecutor(pool->executor(), seedRecoveryFixture(pool), "seed recovery fixture");
        seedRunningBatch(batch_repository, fixture);

        assertMetadataRecovery(catalog_repository, fixture);
        assertBatchRecovery(batch_repository, fixture);

        std::cout << "MiguMetadataRecoveryIntegrationTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguMetadataRecoveryIntegrationTest failed: " << e.what() << std::endl;
        return 1;
    }
}
