#include "migu/support/MySqlTestSupport.h"

#include "crawler/application/model/MusicCatalog.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"
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

// 补池认领测试 fixture 的平台 ID。
struct ClaimFixture {
    std::string suffix;              // 唯一后缀
    std::string singer_failed;       // 歌曲失败的歌手 ID
    std::string singer_completed;    // 已完成歌手 ID
    std::string singer_partial;      // 部分完成歌手 ID
    std::string singer_has_content;  // 已有真实歌曲但状态待处理的歌手 ID
    std::string album_detail_failed; // 详情失败专辑 ID
    std::string album_detail_done;   // 详情已完成专辑 ID
    std::string album_songs_failed;  // 专辑歌曲失败且详情完成的专辑 ID
    std::string album_songs_blocked; // 专辑歌曲失败但详情未完成的专辑 ID
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
    std::string value = "codexclaim" + tag + suffix;
    value.resize(64, '0');
    return value;
}

// 执行 SQL 并忽略结果。
boost::asio::awaitable<void> executeSql(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    (void)co_await pool->coExecute(std::move(sql));
}

// 清理补池认领测试 fixture。
boost::asio::awaitable<void> cleanupClaimFixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    co_await executeSql(pool, "DELETE FROM songs WHERE normalized_title LIKE 'codex_claim_%'");
    co_await executeSql(pool, "DELETE FROM albums WHERE normalized_title LIKE 'codex_claim_%'");
    co_await executeSql(pool, "DELETE FROM singers WHERE normalized_name LIKE 'codex_claim_%'");
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
                support::runOnExecutor(pool_->executor(), cleanupClaimFixtures(pool_), "cleanup claim fixtures");
            }
        } catch (...) {
        }
    }

private:
    std::shared_ptr<mysql::MySqlConnectionPool> pool_; // 测试连接池
};

// 插入一个歌手和对应来源。
boost::asio::awaitable<void> seedSinger(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &suffix,
    const std::string &tag,
    const std::string &source_singer_id,
    int songs_status,
    int albums_status
)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string name = "Codex Claim Singer " + tag + " " + suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + name + "', 'codex_claim_singer_" + tag + "_" + suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_singer_id, source_gender, source_name_pinyin, source_country, songs_sync_status, songs_sync_error, albums_sync_status, albums_sync_error, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', '" + source_singer_id + "', 1, 'codex', 'huayu', " + std::to_string(songs_status) + ", 'seed songs error', " + std::to_string(albums_status) + ", 'seed albums error', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
}

// 插入一个已有真实歌曲关系但同步字段仍待处理的歌手。
boost::asio::awaitable<void> seedSingerWithContent(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &suffix,
    const std::string &source_singer_id
)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string name = "Codex Claim Singer Has Content " + suffix;
    co_await connection->async_execute(
        "INSERT INTO singers (name, normalized_name, gender, status, created_at, updated_at) VALUES ('" + name + "', 'codex_claim_singer_has_content_" + suffix + "', 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto singer_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO singer_source_refs (singer_db_id, source, source_singer_id, source_gender, source_name_pinyin, source_country, source_song_total, songs_sync_status, albums_sync_status, created_at, updated_at) VALUES (" + std::to_string(singer_db_id) + ", 'migu', '" + source_singer_id + "', 1, 'codex', 'huayu', 1, 0, 0, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );

    const std::string song_title = "Codex Claim Song Has Content " + suffix;
    co_await connection->async_execute(
        "INSERT INTO songs (title, normalized_title, created_at, updated_at) VALUES ('" + song_title + "', 'codex_claim_song_has_content_" + suffix + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO song_source_refs (song_db_id, source, source_song_id, source_content_id, source_resource_type, metadata_status, created_at, updated_at) VALUES (" + std::to_string(song_db_id) + ", 'migu', 'claim-song-" + suffix + "', 'claim-content-" + suffix + "', '2', 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto song_source_ref_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO song_artist_refs (song_source_ref_id, singer_source_ref_id, artist_role, artist_order, is_primary, is_seed, created_at, updated_at) VALUES (" + std::to_string(song_source_ref_id) + ", (SELECT id FROM singer_catalog_sources WHERE source = 'migu' AND source_singer_id = '" + source_singer_id + "' LIMIT 1), 'artist', 1, 1, 1, " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
}

// 插入一个专辑和对应来源。
boost::asio::awaitable<void> seedAlbum(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &suffix,
    const std::string &tag,
    const std::string &source_album_id,
    int detail_status,
    int songs_status
)
{
    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string title = "Codex Claim Album " + tag + " " + suffix;
    co_await connection->async_execute(
        "INSERT INTO albums (title, normalized_title, release_date, identity_hash, created_at, updated_at) VALUES ('" + title + "', 'codex_claim_album_" + tag + "_" + suffix + "', '2026-01-01', '" + identityHash(suffix, tag) + "', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await connection->async_execute("SELECT LAST_INSERT_ID()", result, boost::asio::use_awaitable);
    const auto album_db_id = mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    co_await connection->async_execute(
        "INSERT INTO album_source_refs (album_db_id, source, source_album_id, source_resource_type, source_album_kind, detail_sync_status, detail_sync_error, songs_sync_status, songs_sync_error, created_at, updated_at) VALUES (" + std::to_string(album_db_id) + ", 'migu', '" + source_album_id + "', '5', 2, " + std::to_string(detail_status) + ", 'seed detail error', " + std::to_string(songs_status) + ", 'seed songs error', " + std::to_string(now) + ", " + std::to_string(now) + ")",
        result,
        boost::asio::use_awaitable
    );
    co_await pool->coExecute("INSERT INTO album_artist_refs(album_source_ref_id,singer_source_ref_id,artist_role,artist_order,is_primary) SELECT a.id,s.id,'artist',1,1 FROM album_catalog_sources a JOIN singer_catalog_sources s ON s.source=a.source AND s.source_singer_id='claim-artist' WHERE a.source='migu' AND a.source_album_id='" + source_album_id + "' ON DUPLICATE KEY UPDATE is_primary=1");
}

// 插入完整认领测试数据。
boost::asio::awaitable<ClaimFixture> seedClaimFixture(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    ClaimFixture fixture;
    fixture.suffix = uniqueSuffix();
    fixture.singer_failed = "950" + fixture.suffix + "1";
    fixture.singer_completed = "950" + fixture.suffix + "2";
    fixture.singer_partial = "950" + fixture.suffix + "3";
    fixture.singer_has_content = "950" + fixture.suffix + "4";
    fixture.album_detail_failed = "850" + fixture.suffix + "1";
    fixture.album_detail_done = "850" + fixture.suffix + "2";
    fixture.album_songs_failed = "850" + fixture.suffix + "3";
    fixture.album_songs_blocked = "850" + fixture.suffix + "4";

    co_await seedSinger(pool, fixture.suffix, "failed", fixture.singer_failed, 3, 3);
    co_await seedSinger(pool, fixture.suffix, "done", fixture.singer_completed, 2, 2);
    co_await seedSinger(pool, fixture.suffix, "partial", fixture.singer_partial, 4, 4);
    co_await seedSingerWithContent(pool, fixture.suffix, fixture.singer_has_content);
    co_await seedAlbum(pool, fixture.suffix, "detail_failed", fixture.album_detail_failed, 3, 0);
    co_await seedAlbum(pool, fixture.suffix, "detail_done", fixture.album_detail_done, 2, 0);
    co_await seedAlbum(pool, fixture.suffix, "songs_failed", fixture.album_songs_failed, 2, 3);
    co_await seedAlbum(pool, fixture.suffix, "songs_blocked", fixture.album_songs_blocked, 0, 3);
    co_return fixture;
}

// 验证歌手补池认领会跳过已完成和不存在目标。
void assertSingerClaims(mysql::MySqlMusicCatalogQueryRepository &repository, const ClaimFixture &fixture)
{
    model::MiguBackfillTargetQuery explicit_query;
    explicit_query.source_ids = {fixture.singer_failed, fixture.singer_completed, "missing-" + fixture.suffix};
    explicit_query.statuses = {3};
    auto songs = repository.claimSingerSongs(explicit_query);
    require(songs.claimed_count == 1, "claimSingerSongs should claim one failed singer");
    require(songs.skipped_count == 2, "claimSingerSongs should skip completed and missing singers");
    require(songs.singers.front().source_singer_id == fixture.singer_failed, "unexpected claimed singer songs target");
    require(repository.findSinger("migu", fixture.singer_failed)->songs_sync_status == 1, "claimed singer songs status should be running");

    auto duplicate = repository.claimSingerSongs(explicit_query);
    require(duplicate.claimed_count == 0, "second singer songs claim must not reclaim running target");

    model::MiguBackfillTargetQuery content_query;
    content_query.source_ids = {fixture.singer_has_content};
    content_query.statuses = {0};
    auto content_claim = repository.claimSingerSongs(content_query);
    require(content_claim.claimed_count == 0, "claimSingerSongs should skip singer with real song refs");
    auto content_singer = repository.findSinger("migu", fixture.singer_has_content);
    require(content_singer.has_value(), "content singer should exist");
    require(content_singer->songs_sync_count == 1, "content singer should expose real song count");
    require(content_singer->songs_sync_status == 2, "content singer should expose completed song status");

    model::MiguBackfillTargetQuery filter_query;
    filter_query.statuses = {4};
    filter_query.country = "huayu";
    filter_query.gender = 1;
    filter_query.limit = 10;
    auto albums = repository.claimSingerAlbums(filter_query);
    require(albums.claimed_count == 1, "claimSingerAlbums should claim one partial singer by filter");
    require(albums.singers.front().source_singer_id == fixture.singer_partial, "unexpected claimed singer albums target");

    auto second_filter = repository.claimSingerAlbums(filter_query);
    require(second_filter.claimed_count == 0, "second filtered singer albums claim must not duplicate target");
}

// 验证专辑补池认领会跳过已完成详情和详情未完成的歌曲目标。
void assertAlbumClaims(mysql::MySqlMusicCatalogQueryRepository &repository, const ClaimFixture &fixture)
{
    model::MiguBackfillTargetQuery detail_query;
    detail_query.source_ids = {fixture.album_detail_failed, fixture.album_detail_done, "missing-" + fixture.suffix};
    detail_query.statuses = {3};
    auto details = repository.claimAlbumDetails(detail_query);
    require(details.claimed_count == 1, "claimAlbumDetails should claim one failed album");
    require(details.skipped_count == 2, "claimAlbumDetails should skip completed and missing albums");
    require(repository.findAlbum("migu", fixture.album_detail_failed)->detail_sync_status == 1, "claimed album detail status should be running");

    model::MiguBackfillTargetQuery songs_query;
    songs_query.source_ids = {fixture.album_songs_failed, fixture.album_songs_blocked, "missing-" + fixture.suffix};
    songs_query.statuses = {3};
    auto songs = repository.claimAlbumSongs(songs_query);
    require(songs.claimed_count == 1, "claimAlbumSongs should claim one album with completed detail");
    require(songs.skipped_count == 2, "claimAlbumSongs should skip blocked and missing albums");
    require(repository.findAlbum("migu", fixture.album_songs_failed)->songs_sync_status == 1, "claimed album songs status should be running");

    auto duplicate = repository.claimAlbumSongs(songs_query);
    require(duplicate.claimed_count == 0, "second album songs claim must not reclaim running target");
}

// 验证主入口与补池强制重采的资格一致，且不会抢占运行中的任务。
void assertForcedClaims(mysql::MySqlMusicCatalogQueryRepository &repository, const ClaimFixture &fixture, const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlSingerSourceSyncQueue queue(pool);
    model::SingerSourceSyncQuery query;
    query.keyword = fixture.singer_completed;
    require(queue.previewPendingSongs(query).eligible_count == 0, "normal preview must exclude completed singer");
    query.force = true;
    require(queue.previewPendingSongs(query).eligible_count == 1, "forced preview must include completed singer");
    require(queue.claimPendingSongs(query).size() == 1, "forced main queue must claim completed singer");
    require(queue.previewPendingSongs(query).eligible_count == 0, "forced preview must exclude active singer");
    require(queue.claimPendingSongs(query).empty(), "forced main queue must not reclaim active singer");

    query.keyword = fixture.singer_has_content;
    query.force = false;
    require(queue.previewPendingSongs(query).eligible_count == 0, "normal queue must honor real-count completion");
    query.force = true;
    require(queue.previewPendingSongs(query).eligible_count == 1, "forced queue must include real-count completion");
    require(queue.claimPendingSongs(query).size() == 1, "forced queue must bypass real-count completion guard");
    support::runOnExecutor(pool->executor(), executeSql(pool, "UPDATE singer_source_refs SET source_song_total=2 WHERE source='migu' AND source_singer_id='" + fixture.singer_has_content + "'"), "make running singer incomplete");
    require(queue.previewPendingSongs(query).eligible_count == 0 && queue.claimPendingSongs(query).empty(), "incomplete count must not bypass active claim protection");

    model::MiguBackfillTargetQuery retry;
    retry.force = true;
    retry.source_ids = {fixture.singer_has_content};
    require(repository.claimSingerSongs(retry).claimed_count == 0, "forced backfill must not steal incomplete active singer");
    retry.source_ids = {fixture.singer_completed};
    require(repository.claimSingerAlbums(retry).claimed_count == 1, "forced backfill must claim completed singer albums");
    require(repository.claimSingerAlbums(retry).claimed_count == 0, "forced backfill must not reclaim active singer albums");
    query.keyword = fixture.singer_completed;
    require(queue.previewPendingAlbums(query).eligible_count == 0 && queue.claimPendingAlbums(query).empty(), "main queue must respect backfill album claims");

    retry.source_ids = {fixture.album_detail_done};
    require(repository.claimAlbumDetails(retry).claimed_count == 1, "forced backfill must claim completed album details");
    require(repository.claimAlbumDetails(retry).claimed_count == 0, "forced backfill must not reclaim active album details");
    retry.source_ids = {fixture.album_songs_blocked};
    require(repository.claimAlbumSongs(retry).claimed_count == 0, "force must preserve album detail prerequisite");
    support::runOnExecutor(pool->executor(), executeSql(pool, "UPDATE album_source_refs SET detail_sync_status=2,songs_sync_status=2 WHERE source='migu' AND source_album_id='" + fixture.album_detail_done + "'"), "prepare completed album tracks");
    retry.source_ids = {fixture.album_detail_done};
    require(repository.claimAlbumSongs(retry).claimed_count == 1, "forced backfill must claim completed album tracks");
    require(repository.claimAlbumSongs(retry).claimed_count == 0, "forced backfill must not reclaim active album tracks");
}

} // namespace

int main()
{
    try {
        auto pool = support::makeInitializedPool();
        FixtureGuard guard(pool);
        support::runOnExecutor(pool->executor(), cleanupClaimFixtures(pool), "initial claim fixture cleanup");

        mysql::MySqlMusicCatalogQueryRepository repository(pool);
        const auto fixture = support::runOnExecutor(pool->executor(), seedClaimFixture(pool), "seed claim fixture");

        assertSingerClaims(repository, fixture);
        assertAlbumClaims(repository, fixture);
        assertForcedClaims(repository, fixture, pool);

        std::cout << "MiguBackfillClaimIntegrationTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguBackfillClaimIntegrationTest failed: " << e.what() << std::endl;
        return 1;
    }
}
