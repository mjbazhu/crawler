#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = infrastructure::music::persistence::sql;

constexpr int kNoResourceStatus = -999;

// 阻塞等待 MySQL 查询结果，避免 application 感知 Asio。
template <typename T>
T waitForResult(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view context, T fallback)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlMusicCatalogQueryRepository] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlMusicCatalogQueryRepository] " << context << " failed with unknown error" << std::endl;
    }
    return fallback;
}

// 从 COUNT 查询结果读取数值。
std::uint64_t scalarCount(const boost::mysql::results &result)
{
    if (result.rows().empty()) {
        return 0;
    }
    return MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 读取状态分布查询。
void readDistribution(const boost::mysql::results &result, std::map<int, std::uint64_t> &target)
{
    for (boost::mysql::row_view row : result.rows()) {
        target[MySqlValueReader::intAt(row, 0)] = MySqlValueReader::uint64At(row, 1);
    }
}

// 归一化分页，避免前端传入异常值拖垮查询。
void normalizePage(application::model::CatalogPageQuery &query)
{
    query.page = (std::max)(1, query.page);
    query.page_size = (std::min)((std::max)(1, query.page_size), 200);
}

// 将状态数组展开为固定 5 个占位符。
std::array<int, 5> statusSlots(const std::vector<int> &statuses)
{
    std::array<int, 5> slots {-999, -999, -999, -999, -999};
    for (std::size_t index = 0; index < statuses.size() && index < slots.size(); ++index) {
        slots[index] = statuses[index];
    }
    return slots;
}

// 判断状态是否在列表中。
bool containsStatus(const std::vector<int> &statuses, int status)
{
    return std::find(statuses.begin(), statuses.end(), status) != statuses.end();
}

// 资源 preview 默认可操作状态按动作收敛，避免把已完成和永久跳过任务误纳入处理范围。
std::vector<int> eligibleResourceStatuses(const application::model::MiguResourceActionQuery &query)
{
    std::vector<int> values;
    if (query.action == "process") {
        if (query.statuses.empty()) {
            return {0, 3};
        }
        for (int status : query.statuses) {
            if ((status == 0 || status == 3) && !containsStatus(values, status)) {
                values.push_back(status);
            }
        }
        return values.empty() ? std::vector<int> {kNoResourceStatus} : values;
    }
    if (query.action == "retry") {
        if (query.statuses.empty()) {
            return {3};
        }
        for (int status : query.statuses) {
            if (status == 3 && !containsStatus(values, status)) {
                values.push_back(status);
            }
        }
        return values.empty() ? std::vector<int> {kNoResourceStatus} : values;
    }
    if (query.action == "reset") {
        if (query.statuses.empty()) {
            return {0, 1, 3};
        }
        for (int status : query.statuses) {
            if ((status == 0 || status == 1 || status == 3) && !containsStatus(values, status)) {
                values.push_back(status);
            }
        }
        return values.empty() ? std::vector<int> {kNoResourceStatus} : values;
    }
    if (query.action == "recover-stale") {
        return {1};
    }
    return {};
}

// 资源 preview 明细转换，只表达前端治理所需的稳定字段。
application::model::MiguResourcePreviewItem previewItemFromResource(const application::model::MiguResourceQueueRecord &resource, const application::model::MiguResourceActionQuery &query)
{
    const auto allowed_statuses = eligibleResourceStatuses(query);
    application::model::MiguResourcePreviewItem item;
    item.resource_id = resource.resource_id;
    item.target_id = resource.source_song_id.empty() ? resource.source_content_id : resource.source_song_id;
    item.target_name = resource.title;
    item.resource_kind = resource.resource_kind;
    item.format_type = resource.format_type;
    item.sync_status = resource.status;
    item.eligible = containsStatus(allowed_statuses, resource.status);
    if (query.action == "recover-stale" && item.eligible) {
        const auto stale_seconds = static_cast<std::uint64_t>((std::max)(1, query.stale_after_seconds));
        item.eligible = resource.updated_at != 0 && resource.updated_at + stale_seconds <= static_cast<std::uint64_t>(std::time(nullptr));
    }
    if (item.eligible) {
        item.reason = query.action == "process" ? "eligible_for_process" : (query.action == "retry" ? "eligible_for_retry" : (query.action == "reset" ? "eligible_for_reset" : "eligible_for_recover_stale"));
    } else if (query.action == "recover-stale" && resource.status == 1) {
        item.reason = "running_not_stale";
    } else if (query.action == "process" && resource.status == 1) {
        item.reason = "running";
    } else if (resource.status == 2) {
        item.reason = "completed";
    } else if (resource.status == 4) {
        item.reason = "skipped";
    } else {
        item.reason = "status_not_eligible";
    }
    return item;
}

// 状态过滤是否启用。
int hasStatuses(const std::vector<int> &statuses)
{
    return statuses.empty() ? 0 : 1;
}

// 可选 int 转换为 SQL 哨兵值。
int optionalOrMinusOne(std::optional<int> value)
{
    return value ? *value : -1;
}

// 判断歌手列表是否需要先按真实关系数计算暴露状态，再做过滤。
bool requiresSingerSummaryFilter(const application::model::MiguSingerCatalogQuery &query)
{
    return !query.songs_sync_statuses.empty()
           || !query.albums_sync_statuses.empty()
           || query.min_songs_count.has_value()
           || query.max_songs_count.has_value()
           || query.min_albums_count.has_value()
           || query.max_albums_count.has_value();
}

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

} // namespace

MySqlMusicCatalogQueryRepository::MySqlMusicCatalogQueryRepository(std::shared_ptr<MySqlConnectionPool> pool) : MySqlMusicCatalogQueryRepository(std::move(pool), SqlPaths {})
{}

MySqlMusicCatalogQueryRepository::MySqlMusicCatalogQueryRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                                                singer_list_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.singer_list)),
                                                                                                                                singer_count_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.singer_count)),
                                                                                                                                singer_list_filtered_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.singer_list_filtered)),
                                                                                                                                singer_count_filtered_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.singer_count_filtered)),
                                                                                                                                singer_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.singer_find)),
                                                                                                                                album_list_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.album_list)),
                                                                                                                                album_count_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.album_count)),
                                                                                                                                album_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.album_find)),
                                                                                                                                song_list_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.song_list)),
                                                                                                                                song_count_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.song_count)),
                                                                                                                                song_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.song_find)),
                                                                                                                                resource_list_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_list)),
                                                                                                                                resource_count_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_count)),
                                                                                                                                resource_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_find)),
                                                                                                                                resource_retry_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_retry)),
                                                                                                                                resource_reset_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_reset)),
                                                                                                                                resource_recover_stale_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.resource_recover_stale)),
                                                                                                                                governance_event_insert_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.governance_event_insert)),
                                                                                                                                governance_event_list_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.governance_event_list)),
                                                                                                                                governance_event_count_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.governance_event_count)),
                                                                                                                                integrity_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.integrity)),
                                                                                                                                schema_status_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.schema_status)),
                                                                                                                                album_source_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.album_source_find)),
                                                                                                                                claim_singer_songs_select_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_singer_songs_select)),
                                                                                                                                claim_singer_songs_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_singer_songs_update)),
                                                                                                                                claim_singer_albums_select_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_singer_albums_select)),
                                                                                                                                claim_singer_albums_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_singer_albums_update)),
                                                                                                                                claim_album_details_select_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_album_details_select)),
                                                                                                                                claim_album_details_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_album_details_update)),
                                                                                                                                claim_album_songs_select_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_album_songs_select)),
                                                                                                                                claim_album_songs_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_album_songs_update)),
                                                                                                                                reset_singer_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.reset_singer_songs)),
                                                                                                                                reset_singer_albums_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.reset_singer_albums)),
                                                                                                                                reset_album_details_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.reset_album_details)),
                                                                                                                                reset_album_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.reset_album_songs)),
                                                                                                                                recover_singer_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.recover_singer_songs)),
                                                                                                                                recover_singer_albums_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.recover_singer_albums)),
                                                                                                                                recover_album_details_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.recover_album_details)),
                                                                                                                                recover_album_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.recover_album_songs))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlMusicCatalogQueryRepository] pool must not be null");
    }
    overview_sql_.emplace("sql/mysql/singers/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/singers/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/singer_source_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/singer_source_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/songs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/songs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/song_source_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/song_source_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/albums/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/albums/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/album_source_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/album_source_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/song_artist_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/song_artist_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/album_artist_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/album_artist_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/album_song_refs/overview_count.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/album_song_refs/overview_count.sql"));
    overview_sql_.emplace("sql/mysql/singer_source_refs/overview_songs_sync_status.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/singer_source_refs/overview_songs_sync_status.sql"));
    overview_sql_.emplace("sql/mysql/singer_source_refs/overview_albums_sync_status.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/singer_source_refs/overview_albums_sync_status.sql"));
    overview_sql_.emplace("sql/mysql/album_source_refs/overview_detail_sync_status.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/album_source_refs/overview_detail_sync_status.sql"));
    overview_sql_.emplace("sql/mysql/album_source_refs/overview_songs_sync_status.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/album_source_refs/overview_songs_sync_status.sql"));
    overview_sql_.emplace("sql/mysql/song_resources/overview_status.sql", sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/song_resources/overview_status.sql"));
}

application::model::MusicOverview MySqlMusicCatalogQueryRepository::overview()
{
    return waitForResult(pool_->executor(), coOverview(), "overview", application::model::MusicOverview {});
}

application::model::MiguSingerCatalogPage MySqlMusicCatalogQueryRepository::listSingers(application::model::MiguSingerCatalogQuery query)
{
    return waitForResult(pool_->executor(), coListSingers(std::move(query)), "listSingers", application::model::MiguSingerCatalogPage {});
}

std::optional<application::model::MiguSingerCatalogRecord> MySqlMusicCatalogQueryRepository::findSinger(std::string source, std::string source_singer_id)
{
    return waitForResult<std::optional<application::model::MiguSingerCatalogRecord>>(pool_->executor(), coFindSinger(std::move(source), std::move(source_singer_id)), "findSinger", std::nullopt);
}

application::model::MiguAlbumCatalogPage MySqlMusicCatalogQueryRepository::listAlbums(application::model::MiguAlbumCatalogQuery query)
{
    return waitForResult(pool_->executor(), coListAlbums(std::move(query)), "listAlbums", application::model::MiguAlbumCatalogPage {});
}

std::optional<application::model::MiguAlbumCatalogRecord> MySqlMusicCatalogQueryRepository::findAlbum(std::string source, std::string source_album_id)
{
    return waitForResult<std::optional<application::model::MiguAlbumCatalogRecord>>(pool_->executor(), coFindAlbum(std::move(source), std::move(source_album_id)), "findAlbum", std::nullopt);
}

application::model::MiguSongCatalogPage MySqlMusicCatalogQueryRepository::listSongs(application::model::MiguSongCatalogQuery query)
{
    return waitForResult(pool_->executor(), coListSongs(std::move(query)), "listSongs", application::model::MiguSongCatalogPage {});
}

std::optional<application::model::MiguSongCatalogRecord> MySqlMusicCatalogQueryRepository::findSong(std::string source, std::string source_song_id)
{
    return waitForResult<std::optional<application::model::MiguSongCatalogRecord>>(pool_->executor(), coFindSong(std::move(source), std::move(source_song_id)), "findSong", std::nullopt);
}

application::model::MiguResourceQueuePage MySqlMusicCatalogQueryRepository::listResources(application::model::MiguResourceQueueQuery query)
{
    return waitForResult(pool_->executor(), coListResources(std::move(query)), "listResources", application::model::MiguResourceQueuePage {});
}

std::optional<application::model::MiguResourceQueueRecord> MySqlMusicCatalogQueryRepository::findResource(std::uint64_t resource_id)
{
    return waitForResult<std::optional<application::model::MiguResourceQueueRecord>>(pool_->executor(), coFindResource(resource_id), "findResource", std::nullopt);
}

application::model::MiguResourcePreviewResult MySqlMusicCatalogQueryRepository::previewResources(application::model::MiguResourceActionQuery query)
{
    return waitForResult(pool_->executor(), coPreviewResources(std::move(query)), "previewResources", application::model::MiguResourcePreviewResult {});
}

int MySqlMusicCatalogQueryRepository::retryResources(application::model::MiguResourceActionQuery query)
{
    return waitForResult(pool_->executor(), coRetryResources(std::move(query)), "retryResources", 0);
}

int MySqlMusicCatalogQueryRepository::resetResources(application::model::MiguResourceActionQuery query)
{
    return waitForResult(pool_->executor(), coResetResources(std::move(query)), "resetResources", 0);
}

int MySqlMusicCatalogQueryRepository::recoverStaleResources(application::model::MiguResourceActionQuery query, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleResources(std::move(query), std::move(error)), "recoverStaleResources", 0);
}

application::model::MiguIntegritySummary MySqlMusicCatalogQueryRepository::integrity(std::string source, int stale_after_seconds)
{
    return waitForResult(pool_->executor(), coIntegrity(std::move(source), stale_after_seconds), "integrity", application::model::MiguIntegritySummary {});
}

application::model::MiguSchemaStatus MySqlMusicCatalogQueryRepository::schemaStatus()
{
    return waitForResult(pool_->executor(), coSchemaStatus(), "schemaStatus", application::model::MiguSchemaStatus {});
}

void MySqlMusicCatalogQueryRepository::appendGovernanceEvents(const std::vector<application::model::MiguGovernanceEventRecord> &events)
{
    (void)waitForResult(pool_->executor(), coAppendGovernanceEvents(events), "appendGovernanceEvents", true);
}

application::model::MiguGovernanceEventPage MySqlMusicCatalogQueryRepository::listGovernanceEvents(application::model::MiguGovernanceEventQuery query)
{
    return waitForResult(pool_->executor(), coListGovernanceEvents(std::move(query)), "listGovernanceEvents", application::model::MiguGovernanceEventPage {});
}

std::vector<application::model::AlbumSourceMetadata> MySqlMusicCatalogQueryRepository::findAlbumSourcesByIds(std::string source, const std::vector<std::string> &source_album_ids)
{
    return waitForResult(pool_->executor(), coFindAlbumSourcesByIds(std::move(source), source_album_ids), "findAlbumSourcesByIds", std::vector<application::model::AlbumSourceMetadata> {});
}

application::model::MiguBackfillClaimResult MySqlMusicCatalogQueryRepository::claimSingerSongs(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coClaimSingerSongs(std::move(query)), "claimSingerSongs", application::model::MiguBackfillClaimResult {});
}

application::model::MiguBackfillClaimResult MySqlMusicCatalogQueryRepository::claimSingerAlbums(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coClaimSingerAlbums(std::move(query)), "claimSingerAlbums", application::model::MiguBackfillClaimResult {});
}

application::model::MiguBackfillClaimResult MySqlMusicCatalogQueryRepository::claimAlbumDetails(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coClaimAlbumDetails(std::move(query)), "claimAlbumDetails", application::model::MiguBackfillClaimResult {});
}

application::model::MiguBackfillClaimResult MySqlMusicCatalogQueryRepository::claimAlbumSongs(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coClaimAlbumSongs(std::move(query)), "claimAlbumSongs", application::model::MiguBackfillClaimResult {});
}

int MySqlMusicCatalogQueryRepository::resetSingerSongs(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coResetSingerSongs(std::move(query)), "resetSingerSongs", 0);
}

int MySqlMusicCatalogQueryRepository::resetSingerAlbums(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coResetSingerAlbums(std::move(query)), "resetSingerAlbums", 0);
}

int MySqlMusicCatalogQueryRepository::resetAlbumDetails(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coResetAlbumDetails(std::move(query)), "resetAlbumDetails", 0);
}

int MySqlMusicCatalogQueryRepository::resetAlbumSongs(application::model::MiguBackfillTargetQuery query)
{
    return waitForResult(pool_->executor(), coResetAlbumSongs(std::move(query)), "resetAlbumSongs", 0);
}

int MySqlMusicCatalogQueryRepository::recoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleSingerSongs(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleSingerSongs", 0);
}

int MySqlMusicCatalogQueryRepository::recoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleSingerAlbums(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleSingerAlbums", 0);
}

int MySqlMusicCatalogQueryRepository::recoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleAlbumDetails(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleAlbumDetails", 0);
}

int MySqlMusicCatalogQueryRepository::recoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleAlbumSongs(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleAlbumSongs", 0);
}

boost::asio::awaitable<application::model::MusicOverview> MySqlMusicCatalogQueryRepository::coOverview()
{
    application::model::MusicOverview overview;
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    co_await connection->async_execute(overview_sql_.at("sql/mysql/singers/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.singers = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/singer_source_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.singer_sources = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/songs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.songs = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/song_source_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.song_sources = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/albums/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.albums = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/album_source_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.album_sources = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/song_artist_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.singer_song_relations = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/album_artist_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.singer_album_relations = scalarCount(result);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/album_song_refs/overview_count.sql"), result, boost::asio::use_awaitable);
    overview.album_song_relations = scalarCount(result);

    co_await connection->async_execute(overview_sql_.at("sql/mysql/singer_source_refs/overview_songs_sync_status.sql"), result, boost::asio::use_awaitable);
    readDistribution(result, overview.singer_song_sync);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/singer_source_refs/overview_albums_sync_status.sql"), result, boost::asio::use_awaitable);
    readDistribution(result, overview.singer_album_sync);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/album_source_refs/overview_detail_sync_status.sql"), result, boost::asio::use_awaitable);
    readDistribution(result, overview.album_detail_sync);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/album_source_refs/overview_songs_sync_status.sql"), result, boost::asio::use_awaitable);
    readDistribution(result, overview.album_song_sync);
    co_await connection->async_execute(overview_sql_.at("sql/mysql/song_resources/overview_status.sql"), result, boost::asio::use_awaitable);
    readDistribution(result, overview.resource_status);

    co_return overview;
}

boost::asio::awaitable<application::model::MiguSingerCatalogPage> MySqlMusicCatalogQueryRepository::coListSingers(application::model::MiguSingerCatalogQuery query)
{
    normalizePage(query);
    application::model::MiguSingerCatalogPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto songs = statusSlots(query.songs_sync_statuses);
    const auto albums = statusSlots(query.albums_sync_statuses);
    const int gender = optionalOrMinusOne(query.gender);
    const int min_songs_count = optionalOrMinusOne(query.min_songs_count);
    const int max_songs_count = optionalOrMinusOne(query.max_songs_count);
    const int min_albums_count = optionalOrMinusOne(query.min_albums_count);
    const int max_albums_count = optionalOrMinusOne(query.max_albums_count);
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    const bool summary_filter = requiresSingerSummaryFilter(query);

    if (summary_filter) {
        auto count_statement = co_await connection->async_prepare_statement(singer_count_filtered_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(count_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.source_singer_id, query.source_singer_id, query.country, query.country, gender, gender, hasStatuses(query.songs_sync_statuses), songs[0], songs[1], songs[2], songs[3], songs[4], hasStatuses(query.albums_sync_statuses), albums[0], albums[1], albums[2], albums[3], albums[4], min_songs_count, min_songs_count, max_songs_count, max_songs_count, min_albums_count, min_albums_count, max_albums_count, max_albums_count), result, boost::asio::use_awaitable);
    } else {
        auto count_statement = co_await connection->async_prepare_statement(singer_count_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(count_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.source_singer_id, query.source_singer_id, query.country, query.country, gender, gender), result, boost::asio::use_awaitable);
    }

    page.total = scalarCount(result);
    if (summary_filter) {
        auto list_statement = co_await connection->async_prepare_statement(singer_list_filtered_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(list_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.source_singer_id, query.source_singer_id, query.country, query.country, gender, gender, hasStatuses(query.songs_sync_statuses), songs[0], songs[1], songs[2], songs[3], songs[4], hasStatuses(query.albums_sync_statuses), albums[0], albums[1], albums[2], albums[3], albums[4], min_songs_count, min_songs_count, max_songs_count, max_songs_count, min_albums_count, min_albums_count, max_albums_count, max_albums_count, query.keyword, static_cast<std::uint64_t>(query.page_size), offset, query.keyword), result, boost::asio::use_awaitable);
    } else {
        auto list_statement = co_await connection->async_prepare_statement(singer_list_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(list_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.source_singer_id, query.source_singer_id, query.country, query.country, gender, gender, query.keyword, static_cast<std::uint64_t>(query.page_size), offset, query.keyword), result, boost::asio::use_awaitable);
    }
    for (boost::mysql::row_view row : result.rows()) {
        page.items.push_back(mapSinger(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::MiguSingerCatalogRecord>> MySqlMusicCatalogQueryRepository::coFindSinger(std::string source, std::string source_singer_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(singer_find_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(source), std::move(source_singer_id)), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapSinger(result.rows().front());
}

boost::asio::awaitable<application::model::MiguAlbumCatalogPage> MySqlMusicCatalogQueryRepository::coListAlbums(application::model::MiguAlbumCatalogQuery query)
{
    normalizePage(query);
    application::model::MiguAlbumCatalogPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto details = statusSlots(query.detail_sync_statuses);
    const auto songs = statusSlots(query.songs_sync_statuses);
    const int kind = optionalOrMinusOne(query.album_kind);
    const int min_songs_count = optionalOrMinusOne(query.min_songs_count);
    const int max_songs_count = optionalOrMinusOne(query.max_songs_count);
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    auto count_statement = co_await connection->async_prepare_statement(album_count_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, query.artist, kind, kind, static_cast<int>(query.exclude_shared_release_archives), hasStatuses(query.detail_sync_statuses), details[0], details[1], details[2], details[3], details[4], hasStatuses(query.songs_sync_statuses), songs[0], songs[1], songs[2], songs[3], songs[4], min_songs_count, min_songs_count, max_songs_count, max_songs_count), result, boost::asio::use_awaitable);
    page.total = scalarCount(result);

    auto list_statement = co_await connection->async_prepare_statement(album_list_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, query.artist, kind, kind, static_cast<int>(query.exclude_shared_release_archives), hasStatuses(query.detail_sync_statuses), details[0], details[1], details[2], details[3], details[4], hasStatuses(query.songs_sync_statuses), songs[0], songs[1], songs[2], songs[3], songs[4], min_songs_count, min_songs_count, max_songs_count, max_songs_count, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        page.items.push_back(mapAlbum(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::MiguAlbumCatalogRecord>> MySqlMusicCatalogQueryRepository::coFindAlbum(std::string source, std::string source_album_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(album_find_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(source), std::move(source_album_id)), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapAlbum(result.rows().front());
}

boost::asio::awaitable<application::model::MiguSongCatalogPage> MySqlMusicCatalogQueryRepository::coListSongs(application::model::MiguSongCatalogQuery query)
{
    normalizePage(query);
    application::model::MiguSongCatalogPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto statuses = statusSlots(query.resource_statuses);
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    auto count_statement = co_await connection->async_prepare_statement(song_count_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, query.album, query.album, query.album, query.album, query.source_singer_id, query.source_singer_id, query.source_album_id, query.source_album_id, query.source_album_id, hasStatuses(query.resource_statuses), statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.format_type, query.format_type), result, boost::asio::use_awaitable);
    page.total = scalarCount(result);

    auto list_statement = co_await connection->async_prepare_statement(song_list_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, query.album, query.album, query.album, query.album, query.source_singer_id, query.source_singer_id, query.source_album_id, query.source_album_id, query.source_album_id, hasStatuses(query.resource_statuses), statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.format_type, query.format_type, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        page.items.push_back(mapSong(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::MiguSongCatalogRecord>> MySqlMusicCatalogQueryRepository::coFindSong(std::string source, std::string source_song_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(song_find_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    const std::string target_id = source_song_id;
    co_await connection->async_execute(statement.bind(std::move(source), target_id, target_id), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapSong(result.rows().front());
}

boost::asio::awaitable<application::model::MiguResourceQueuePage> MySqlMusicCatalogQueryRepository::coListResources(application::model::MiguResourceQueueQuery query)
{
    normalizePage(query);
    application::model::MiguResourceQueuePage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto statuses = statusSlots(query.statuses);
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    auto count_statement = co_await connection->async_prepare_statement(resource_count_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.keyword, hasStatuses(query.statuses), statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.resource_kind, query.resource_kind, query.format_type, query.format_type), result, boost::asio::use_awaitable);
    page.total = scalarCount(result);

    auto list_statement = co_await connection->async_prepare_statement(resource_list_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.source, query.keyword, query.keyword, query.keyword, query.keyword, hasStatuses(query.statuses), statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.resource_kind, query.resource_kind, query.format_type, query.format_type, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        page.items.push_back(mapResource(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::MiguResourceQueueRecord>> MySqlMusicCatalogQueryRepository::coFindResource(std::uint64_t resource_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(resource_find_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(resource_id), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapResource(result.rows().front());
}

boost::asio::awaitable<application::model::MiguResourcePreviewResult> MySqlMusicCatalogQueryRepository::coPreviewResources(application::model::MiguResourceActionQuery query)
{
    application::model::MiguResourcePreviewResult preview;
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit <= 0) {
        query.limit = 50;
    }
    query.limit = (std::min)(query.limit, 200);

    if (!query.resource_ids.empty()) {
        std::set<std::uint64_t> seen;
        for (const auto resource_id : query.resource_ids) {
            if (!seen.insert(resource_id).second) {
                continue;
            }
            auto resource = co_await coFindResource(resource_id);
            if (!resource) {
                ++preview.missing_count;
                if (static_cast<int>(preview.items.size()) < query.limit) {
                    application::model::MiguResourcePreviewItem item;
                    item.resource_id = resource_id;
                    item.eligible = false;
                    item.reason = "not_found";
                    preview.items.push_back(std::move(item));
                }
                continue;
            }
            ++preview.matched_count;
            auto item = previewItemFromResource(*resource, query);
            if (item.eligible) {
                ++preview.eligible_count;
            } else {
                ++preview.ineligible_count;
            }
            if (static_cast<int>(preview.items.size()) < query.limit) {
                preview.items.push_back(std::move(item));
            }
        }
        co_return preview;
    }

    application::model::MiguResourceQueueQuery matched_query;
    matched_query.source = query.source;
    matched_query.page = 1;
    matched_query.page_size = (std::min)(query.limit, 50);
    matched_query.keyword = query.keyword;
    matched_query.statuses = query.statuses;
    matched_query.resource_kind = query.resource_kind;
    matched_query.format_type = query.format_type;
    auto matched_page = co_await coListResources(matched_query);
    preview.matched_count = static_cast<int>((std::min<std::uint64_t>)(matched_page.total, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    for (const auto &resource : matched_page.items) {
        auto item = previewItemFromResource(resource, query);
        if (item.eligible) {
            ++preview.eligible_count;
        } else {
            ++preview.ineligible_count;
        }
        preview.items.push_back(std::move(item));
    }

    application::model::MiguResourceQueueQuery eligible_query = matched_query;
    eligible_query.page_size = 1;
    eligible_query.statuses = eligibleResourceStatuses(query);
    if (query.action == "recover-stale") {
        // 仅用状态查询会多算未超时 running，精确值依赖样例明细展示；实际 recover SQL 仍按超时条件执行。
        preview.eligible_count = 0;
        preview.ineligible_count = 0;
        for (const auto &item : preview.items) {
            item.eligible ? ++preview.eligible_count : ++preview.ineligible_count;
        }
    } else {
        auto eligible_page = co_await coListResources(std::move(eligible_query));
        preview.eligible_count = static_cast<int>((std::min<std::uint64_t>)(eligible_page.total, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
        preview.ineligible_count = (std::max)(0, preview.matched_count - preview.eligible_count);
    }
    co_return preview;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRetryResources(application::model::MiguResourceActionQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(resource_retry_sql_, boost::asio::use_awaitable);
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](std::uint64_t resource_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(query.source, resource_id, resource_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.keyword, query.resource_kind, query.resource_kind, query.format_type, query.format_type, limit), result, boost::asio::use_awaitable);
        co_return static_cast<int>(result.affected_rows());
    };
    if (query.resource_ids.empty()) {
        updated += co_await run_one(0, static_cast<std::uint64_t>((std::max)(1, query.limit)));
    } else {
        for (const auto resource_id : query.resource_ids) {
            updated += co_await run_one(resource_id, 1);
        }
    }
    co_return updated;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coResetResources(application::model::MiguResourceActionQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(resource_reset_sql_, boost::asio::use_awaitable);
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](std::uint64_t resource_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(query.source, resource_id, resource_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.keyword, query.resource_kind, query.resource_kind, query.format_type, query.format_type, limit), result, boost::asio::use_awaitable);
        co_return static_cast<int>(result.affected_rows());
    };
    if (query.resource_ids.empty()) {
        updated += co_await run_one(0, static_cast<std::uint64_t>((std::max)(1, query.limit)));
    } else {
        for (const auto resource_id : query.resource_ids) {
            updated += co_await run_one(resource_id, 1);
        }
    }
    co_return updated;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRecoverStaleResources(application::model::MiguResourceActionQuery query, std::string error)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(resource_recover_stale_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    const int stale_seconds = (std::max)(60, query.stale_after_seconds);
    co_await connection->async_execute(statement.bind(std::move(error), query.source, stale_seconds, query.keyword, query.keyword, query.keyword, query.keyword, query.resource_kind, query.resource_kind, query.format_type, query.format_type, static_cast<std::uint64_t>((std::max)(1, query.limit))), result, boost::asio::use_awaitable);
    co_return static_cast<int>(result.affected_rows());
}

boost::asio::awaitable<application::model::MiguIntegritySummary> MySqlMusicCatalogQueryRepository::coIntegrity(std::string source, int stale_after_seconds)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(integrity_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    const int stale_seconds = (std::max)(60, stale_after_seconds);
    co_await connection->async_execute(statement.bind(source, source, source, source, source, source, source, stale_seconds, source, stale_seconds, source, stale_seconds, source, stale_seconds, source, stale_seconds, source, stale_seconds), result, boost::asio::use_awaitable);
    application::model::MiguIntegritySummary summary;
    if (result.rows().empty()) {
        summary.generated_at = nowSeconds();
        co_return summary;
    }
    const auto row = result.rows().front();
    summary.singers_without_songs = MySqlValueReader::uint64At(row, 0);
    summary.singers_without_albums = MySqlValueReader::uint64At(row, 1);
    summary.albums_without_details = MySqlValueReader::uint64At(row, 2);
    summary.albums_without_songs = MySqlValueReader::uint64At(row, 3);
    summary.songs_without_resource_tasks = MySqlValueReader::uint64At(row, 4);
    summary.resources_failed = MySqlValueReader::uint64At(row, 5);
    summary.resources_running_stale = MySqlValueReader::uint64At(row, 6);
    summary.singers_songs_running_stale = MySqlValueReader::uint64At(row, 7);
    summary.singers_albums_running_stale = MySqlValueReader::uint64At(row, 8);
    summary.albums_details_running_stale = MySqlValueReader::uint64At(row, 9);
    summary.albums_songs_running_stale = MySqlValueReader::uint64At(row, 10);
    summary.running_batches_stale = MySqlValueReader::uint64At(row, 11);
    summary.generated_at = MySqlValueReader::uint64At(row, 12);
    co_return summary;
}

boost::asio::awaitable<application::model::MiguSchemaStatus> MySqlMusicCatalogQueryRepository::coSchemaStatus()
{
    application::model::MiguSchemaStatus status;
    status.checked_at = nowSeconds();
    status.latest_available_version = 2026102901;
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    co_await connection->async_execute(schema_status_sql_, result, boost::asio::use_awaitable);

    for (boost::mysql::row_view row : result.rows()) {
        const std::string object_type = MySqlValueReader::stringAt(row, 0);
        const std::string object_name = MySqlValueReader::stringAt(row, 1);
        const std::string object_value = MySqlValueReader::stringAt(row, 2);
        const bool present = MySqlValueReader::intAt(row, 3) != 0;
        if (object_type == "version") {
            try {
                status.schema_version = static_cast<std::uint64_t>(std::stoull(object_value));
            } catch (...) {
                status.schema_version = 0;
            }
        } else if (object_type == "schema_name") {
            status.schema_name = object_value;
        } else if (object_type == "latest_applied_version") {
            try {
                status.latest_applied_version = static_cast<std::uint64_t>(std::stoull(object_value));
            } catch (...) {
                status.latest_applied_version = 0;
            }
        } else if (object_type == "pending_migration" && present) {
            status.pending_migrations.push_back(object_name);
        } else if (object_type == "checksum_mismatch" && present) {
            status.checksum_mismatches.push_back(object_name);
        } else if (object_type == "table" && !present) {
            status.missing_tables.push_back(object_name);
        } else if (object_type == "column" && !present) {
            status.missing_columns.push_back(object_name);
        }
    }

    status.ok = status.schema_version >= 2026052001 && status.latest_applied_version >= status.latest_available_version && status.pending_migrations.empty() && status.checksum_mismatches.empty() && status.missing_tables.empty() && status.missing_columns.empty();
    co_return status;
}

boost::asio::awaitable<bool> MySqlMusicCatalogQueryRepository::coAppendGovernanceEvents(std::vector<application::model::MiguGovernanceEventRecord> events)
{
    if (events.empty()) {
        co_return true;
    }
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(governance_event_insert_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    for (auto &event : events) {
        if (event.created_at == 0) {
            event.created_at = nowSeconds();
        }
        if (event.source.empty()) {
            event.source = "migu";
        }
        co_await connection->async_execute(
            statement.bind(
                event.source,
                event.action,
                event.target_type,
                event.target_id,
                event.target_name,
                event.target_summary,
                event.target_source_id,
                event.target_owner_name,
                event.batch_id,
                event.before_status,
                event.after_status,
                event.result_code,
                event.error_category,
                event.reason,
                event.payload.empty() ? "{}" : event.payload,
                event.created_at
            ),
            result,
            boost::asio::use_awaitable
        );
    }
    co_return true;
}

boost::asio::awaitable<application::model::MiguGovernanceEventPage> MySqlMusicCatalogQueryRepository::coListGovernanceEvents(application::model::MiguGovernanceEventQuery query)
{
    normalizePage(query);
    if (query.source.empty()) {
        query.source = "migu";
    }
    application::model::MiguGovernanceEventPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(governance_event_count_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.source, query.target_type, query.target_type, query.target_id, query.target_id, query.action, query.action, query.batch_id, query.batch_id), result, boost::asio::use_awaitable);
    page.total = scalarCount(result);

    auto list_statement = co_await connection->async_prepare_statement(governance_event_list_sql_, boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.source, query.target_type, query.target_type, query.target_id, query.target_id, query.action, query.action, query.batch_id, query.batch_id, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        page.items.push_back(mapGovernanceEvent(row));
    }
    co_return page;
}

boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> MySqlMusicCatalogQueryRepository::coFindAlbumSourcesByIds(std::string source, std::vector<std::string> source_album_ids)
{
    std::vector<application::model::AlbumSourceMetadata> albums;
    if (source_album_ids.empty()) {
        co_return albums;
    }
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(album_source_find_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    for (const auto &source_album_id : source_album_ids) {
        co_await connection->async_execute(statement.bind(source, source_album_id), result, boost::asio::use_awaitable);
        if (!result.rows().empty()) {
            albums.push_back(mapAlbumSource(result.rows().front()));
        }
    }
    co_return albums;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coResetSingerSongs(application::model::MiguBackfillTargetQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(reset_singer_songs_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](const std::string &source_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results local_result;
        const int gender = optionalOrMinusOne(query.gender);
        co_await connection->async_execute(statement.bind(query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.country, query.country, gender, gender, limit), local_result, boost::asio::use_awaitable);
        co_return static_cast<int>(local_result.affected_rows());
    };
    if (query.source_ids.empty()) {
        updated += co_await run_one("", static_cast<std::uint64_t>(query.limit));
    } else {
        for (const auto &id : query.source_ids) {
            updated += co_await run_one(id, 1);
        }
    }
    co_return updated;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coResetSingerAlbums(application::model::MiguBackfillTargetQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(reset_singer_albums_sql_, boost::asio::use_awaitable);
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](const std::string &source_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results local_result;
        const int gender = optionalOrMinusOne(query.gender);
        co_await connection->async_execute(statement.bind(query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.country, query.country, gender, gender, limit), local_result, boost::asio::use_awaitable);
        co_return static_cast<int>(local_result.affected_rows());
    };
    if (query.source_ids.empty()) {
        updated += co_await run_one("", static_cast<std::uint64_t>(query.limit));
    } else {
        for (const auto &id : query.source_ids) {
            updated += co_await run_one(id, 1);
        }
    }
    co_return updated;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coResetAlbumDetails(application::model::MiguBackfillTargetQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(reset_album_details_sql_, boost::asio::use_awaitable);
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](const std::string &source_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results local_result;
        const int kind = optionalOrMinusOne(query.album_kind);
        co_await connection->async_execute(statement.bind(query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, kind, kind, limit), local_result, boost::asio::use_awaitable);
        co_return static_cast<int>(local_result.affected_rows());
    };
    if (query.source_ids.empty()) {
        updated += co_await run_one("", static_cast<std::uint64_t>(query.limit));
    } else {
        for (const auto &id : query.source_ids) {
            updated += co_await run_one(id, 1);
        }
    }
    co_return updated;
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coResetAlbumSongs(application::model::MiguBackfillTargetQuery query)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(reset_album_songs_sql_, boost::asio::use_awaitable);
    const auto statuses = statusSlots(query.statuses);
    int updated = 0;
    const auto run_one = [&](const std::string &source_id, std::uint64_t limit) -> boost::asio::awaitable<int> {
        boost::mysql::results local_result;
        const int kind = optionalOrMinusOne(query.album_kind);
        co_await connection->async_execute(statement.bind(query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, kind, kind, limit), local_result, boost::asio::use_awaitable);
        co_return static_cast<int>(local_result.affected_rows());
    };
    if (query.source_ids.empty()) {
        updated += co_await run_one("", static_cast<std::uint64_t>(query.limit));
    } else {
        for (const auto &id : query.source_ids) {
            updated += co_await run_one(id, 1);
        }
    }
    co_return updated;
}

application::model::MiguSingerCatalogRecord MySqlMusicCatalogQueryRepository::mapSinger(boost::mysql::row_view row)
{
    application::model::MiguSingerCatalogRecord singer;
    singer.singer_db_id = MySqlValueReader::uint64At(row, 0);
    singer.name = MySqlValueReader::stringAt(row, 1);
    singer.normalized_name = MySqlValueReader::stringAt(row, 2);
    singer.source_name_pinyin = MySqlValueReader::stringAt(row, 3);
    singer.gender = MySqlValueReader::intAt(row, 4);
    singer.status = MySqlValueReader::intAt(row, 5);
    singer.source_ref_id = MySqlValueReader::uint64At(row, 6);
    singer.source = MySqlValueReader::stringAt(row, 7);
    singer.source_item_key = MySqlValueReader::stringAt(row, 8);
    singer.source_singer_id = MySqlValueReader::stringAt(row, 9);
    singer.source_country = MySqlValueReader::stringAt(row, 10);
    singer.source_tag = MySqlValueReader::stringAt(row, 11);
    singer.source_song_total = MySqlValueReader::optionalIntAt(row, 12);
    singer.source_album_total = MySqlValueReader::optionalIntAt(row, 13);
    singer.avatar_url = MySqlValueReader::stringAt(row, 14);
    singer.songs_sync_status = MySqlValueReader::intAt(row, 15);
    singer.songs_synced_at = MySqlValueReader::uint64At(row, 16);
    singer.songs_sync_count = MySqlValueReader::intAt(row, 17);
    singer.songs_sync_error = MySqlValueReader::stringAt(row, 18);
    singer.songs_last_batch_id = MySqlValueReader::stringAt(row, 19);
    singer.songs_running_batch_id = MySqlValueReader::stringAt(row, 20);
    singer.albums_sync_status = MySqlValueReader::intAt(row, 21);
    singer.albums_synced_at = MySqlValueReader::uint64At(row, 22);
    singer.albums_sync_count = MySqlValueReader::intAt(row, 23);
    singer.albums_sync_error = MySqlValueReader::stringAt(row, 24);
    singer.albums_last_batch_id = MySqlValueReader::stringAt(row, 25);
    singer.albums_running_batch_id = MySqlValueReader::stringAt(row, 26);
    singer.created_at = MySqlValueReader::uint64At(row, 27);
    singer.updated_at = MySqlValueReader::uint64At(row, 28);
    return singer;
}

application::model::MiguAlbumCatalogRecord MySqlMusicCatalogQueryRepository::mapAlbum(boost::mysql::row_view row)
{
    application::model::MiguAlbumCatalogRecord album;
    album.album_db_id = MySqlValueReader::uint64At(row, 0);
    album.title = MySqlValueReader::stringAt(row, 1);
    album.normalized_title = MySqlValueReader::stringAt(row, 2);
    album.primary_artist_name = MySqlValueReader::stringAt(row, 3);
    album.release_date = MySqlValueReader::stringAt(row, 4);
    album.source_ref_id = MySqlValueReader::uint64At(row, 5);
    album.source = MySqlValueReader::stringAt(row, 6);
    album.source_item_key = MySqlValueReader::stringAt(row, 7);
    album.source_album_id = MySqlValueReader::stringAt(row, 8);
    album.source_resource_type = MySqlValueReader::stringAt(row, 9);
    album.source_res_type = MySqlValueReader::stringAt(row, 10);
    album.source_album_kind = MySqlValueReader::intAt(row, 11);
    album.source_title = MySqlValueReader::stringAt(row, 12);
    album.source_artist_id = MySqlValueReader::stringAt(row, 13);
    album.source_artist_name = MySqlValueReader::stringAt(row, 14);
    album.cover_url = MySqlValueReader::stringAt(row, 15);
    album.total_count = MySqlValueReader::stringAt(row, 16);
    album.publish_date = MySqlValueReader::stringAt(row, 17);
    album.play_num = MySqlValueReader::uint64At(row, 18);
    album.play_num_desc = MySqlValueReader::stringAt(row, 19);
    album.detail_sync_status = MySqlValueReader::intAt(row, 20);
    album.detail_synced_at = MySqlValueReader::uint64At(row, 21);
    album.detail_sync_error = MySqlValueReader::stringAt(row, 22);
    album.detail_last_batch_id = MySqlValueReader::stringAt(row, 23);
    album.detail_running_batch_id = MySqlValueReader::stringAt(row, 24);
    album.songs_sync_status = MySqlValueReader::intAt(row, 25);
    album.songs_synced_at = MySqlValueReader::uint64At(row, 26);
    album.songs_sync_count = MySqlValueReader::intAt(row, 27);
    album.songs_sync_error = MySqlValueReader::stringAt(row, 28);
    album.songs_last_batch_id = MySqlValueReader::stringAt(row, 29);
    album.songs_running_batch_id = MySqlValueReader::stringAt(row, 30);
    album.created_at = MySqlValueReader::uint64At(row, 31);
    album.updated_at = MySqlValueReader::uint64At(row, 32);
    album.album_songs_count = MySqlValueReader::intAt(row, 33);
    if (row.size() >= 36) {
        album.source_title_pinyin = MySqlValueReader::stringAt(row, 34);
        album.summary = MySqlValueReader::stringAt(row, 35);
    }
    if (row.size() >= 37) album.source_material_id = MySqlValueReader::stringAt(row, 36);
    return album;
}

application::model::MiguSongCatalogRecord MySqlMusicCatalogQueryRepository::mapSong(boost::mysql::row_view row)
{
    application::model::MiguSongCatalogRecord song;
    song.song_db_id = MySqlValueReader::uint64At(row, 0);
    song.title = MySqlValueReader::stringAt(row, 1);
    song.normalized_title = MySqlValueReader::stringAt(row, 2);
    song.source_duration_seconds = static_cast<std::uint32_t>(MySqlValueReader::uint64At(row, 3));
    song.source_ref_id = MySqlValueReader::uint64At(row, 4);
    song.source = MySqlValueReader::stringAt(row, 5);
    song.source_item_key = MySqlValueReader::stringAt(row, 6);
    song.source_song_id = MySqlValueReader::stringAt(row, 7);
    song.source_content_id = MySqlValueReader::stringAt(row, 8);
    song.source_resource_type = MySqlValueReader::stringAt(row, 9);
    song.source_title_pinyin = MySqlValueReader::stringAt(row, 10);
    song.source_album_id = MySqlValueReader::stringAt(row, 11);
    song.source_album_name = MySqlValueReader::stringAt(row, 12);
    song.primary_artist_id = MySqlValueReader::stringAt(row, 13);
    song.primary_artist_name = MySqlValueReader::stringAt(row, 14);
    song.cover_url = MySqlValueReader::stringAt(row, 15);
    song.lyric_url = MySqlValueReader::stringAt(row, 16);
    song.mrc_url = MySqlValueReader::stringAt(row, 17);
    song.trc_url = MySqlValueReader::stringAt(row, 18);
    song.copyright_id = MySqlValueReader::stringAt(row, 19);
    song.metadata_status = MySqlValueReader::intAt(row, 20);
    song.format_types = MySqlValueReader::stringAt(row, 21);
    song.resource_count = MySqlValueReader::intAt(row, 22);
    song.failed_resource_count = MySqlValueReader::intAt(row, 23);
    song.created_at = MySqlValueReader::uint64At(row, 24);
    song.updated_at = MySqlValueReader::uint64At(row, 25);
    return song;
}

application::model::MiguResourceQueueRecord MySqlMusicCatalogQueryRepository::mapResource(boost::mysql::row_view row)
{
    application::model::MiguResourceQueueRecord resource;
    resource.resource_id = MySqlValueReader::uint64At(row, 0);
    resource.song_source_ref_id = MySqlValueReader::uint64At(row, 1);
    resource.source = MySqlValueReader::stringAt(row, 2);
    resource.source_song_id = MySqlValueReader::stringAt(row, 3);
    resource.source_content_id = MySqlValueReader::stringAt(row, 4);
    resource.title = MySqlValueReader::stringAt(row, 5);
    resource.artist = MySqlValueReader::stringAt(row, 6);
    resource.album = MySqlValueReader::stringAt(row, 7);
    resource.resource_kind = MySqlValueReader::stringAt(row, 8);
    resource.format_type = MySqlValueReader::stringAt(row, 9);
    resource.remote_url = MySqlValueReader::stringAt(row, 10);
    resource.local_path = MySqlValueReader::stringAt(row, 11);
    resource.cloud_backend = MySqlValueReader::stringAt(row, 12);
    resource.cloud_path = MySqlValueReader::stringAt(row, 13);
    resource.size_bytes = MySqlValueReader::uint64At(row, 14);
    resource.checksum = MySqlValueReader::stringAt(row, 15);
    resource.status = MySqlValueReader::intAt(row, 16);
    resource.retry_count = MySqlValueReader::intAt(row, 17);
    resource.last_error = MySqlValueReader::stringAt(row, 18);
    resource.error_category = MySqlValueReader::stringAt(row, 19);
    resource.result_code = MySqlValueReader::stringAt(row, 20);
    resource.last_batch_id = MySqlValueReader::stringAt(row, 21);
    resource.running_batch_id = MySqlValueReader::stringAt(row, 22);
    resource.last_processed_at = MySqlValueReader::uint64At(row, 23);
    resource.fetched_at = MySqlValueReader::uint64At(row, 24);
    resource.cloud_uploaded_at = MySqlValueReader::uint64At(row, 25);
    resource.created_at = MySqlValueReader::uint64At(row, 26);
    resource.updated_at = MySqlValueReader::uint64At(row, 27);
    return resource;
}

application::model::MiguGovernanceEventRecord MySqlMusicCatalogQueryRepository::mapGovernanceEvent(boost::mysql::row_view row)
{
    application::model::MiguGovernanceEventRecord event;
    event.event_id = MySqlValueReader::uint64At(row, 0);
    event.source = MySqlValueReader::stringAt(row, 1);
    event.action = MySqlValueReader::stringAt(row, 2);
    event.target_type = MySqlValueReader::stringAt(row, 3);
    event.target_id = MySqlValueReader::stringAt(row, 4);
    event.target_name = MySqlValueReader::stringAt(row, 5);
    event.target_summary = MySqlValueReader::stringAt(row, 6);
    event.target_source_id = MySqlValueReader::stringAt(row, 7);
    event.target_owner_name = MySqlValueReader::stringAt(row, 8);
    event.batch_id = MySqlValueReader::stringAt(row, 9);
    event.before_status = MySqlValueReader::stringAt(row, 10);
    event.after_status = MySqlValueReader::stringAt(row, 11);
    event.result_code = MySqlValueReader::stringAt(row, 12);
    event.error_category = MySqlValueReader::stringAt(row, 13);
    event.reason = MySqlValueReader::stringAt(row, 14);
    event.payload = MySqlValueReader::stringAt(row, 15);
    event.created_at = MySqlValueReader::uint64At(row, 16);
    return event;
}

application::model::AlbumSourceMetadata MySqlMusicCatalogQueryRepository::mapAlbumSource(boost::mysql::row_view row)
{
    application::model::AlbumSourceMetadata album;
    album.source = MySqlValueReader::stringAt(row, 0);
    album.source_album_id = MySqlValueReader::stringAt(row, 2);
    album.source_resource_type = MySqlValueReader::stringAt(row, 3);
    album.source_res_type = MySqlValueReader::stringAt(row, 4);
    album.source_action = MySqlValueReader::stringAt(row, 5);
    album.source_album_kind = MySqlValueReader::intAt(row, 6);
    album.source_artist_id = MySqlValueReader::stringAt(row, 8);
    album.source_artist_name = MySqlValueReader::stringAt(row, 9);
    album.cover_url = MySqlValueReader::stringAt(row, 10);
    album.total_count = MySqlValueReader::stringAt(row, 11);
    album.detail_sync_status = MySqlValueReader::intAt(row, 12);
    album.title = MySqlValueReader::stringAt(row, 13);
    album.normalized_title = MySqlValueReader::stringAt(row, 14);
    album.artist_name = MySqlValueReader::stringAt(row, 15);
    if (row.size() >= 17) album.source_material_id = MySqlValueReader::stringAt(row, 16);
    return album;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
