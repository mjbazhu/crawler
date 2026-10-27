#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

constexpr std::string_view kClaimOwner = "crawler-migu";

// 普通重试选择未同步、失败和部分完成；force 额外纳入已完成，始终排除运行态。
std::array<int, 5> claimStatusSlots(const std::vector<int> &statuses, bool force)
{
    std::array<int, 5> slots {-999, -999, -999, -999, -999};
    std::vector<int> values;
    values.reserve(statuses.empty() ? 3 : statuses.size());
    for (int status : statuses) {
        if ((status == 0 || status == 3 || status == 4 || (force && status == 2)) && std::find(values.begin(), values.end(), status) == values.end()) {
            values.push_back(status);
        }
    }
    if (statuses.empty()) values = {0, 3, 4};
    if (force && std::find(values.begin(), values.end(), 2) == values.end()) values.push_back(2);

    for (std::size_t index = 0; index < values.size() && index < slots.size(); ++index) {
        slots[index] = values[index];
    }
    return slots;
}

// 将可选整数转为 SQL 过滤哨兵。
int optionalOrMinusOne(std::optional<int> value)
{
    return value ? *value : -1;
}

// 规整补池认领请求，限制单次认领规模。
application::model::MiguBackfillTargetQuery normalizeClaimQuery(application::model::MiguBackfillTargetQuery query)
{
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit <= 0) {
        query.limit = 50;
    }
    query.limit = (std::min)(query.limit, 1000);
    return query;
}

// 生成本次数据库认领令牌，用于 UPDATE 后精确回查本批目标。
std::string makeClaimToken(std::string_view type)
{
    static std::atomic<std::uint64_t> counter {0};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::string(type) + ":" + std::to_string(now) + ":" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

// 根据请求和认领数量计算跳过数量。
void finalizeClaimCounts(application::model::MiguBackfillClaimResult &result, const application::model::MiguBackfillTargetQuery &query)
{
    result.claimed_count = static_cast<int>(result.singers.size() + result.albums.size());
    if (!query.source_ids.empty()) {
        result.requested_count = static_cast<int>(query.source_ids.size());
    }
    result.skipped_count = (std::max)(0, result.requested_count - result.claimed_count);
}

} // namespace

boost::asio::awaitable<application::model::MiguBackfillClaimResult> MySqlMusicCatalogQueryRepository::coClaimSingerSongs(application::model::MiguBackfillTargetQuery query)
{
    query = normalizeClaimQuery(std::move(query));
    auto connection = co_await pool_->coAcquire();
    auto update_statement = co_await connection->async_prepare_statement(claim_singer_songs_update_sql_, boost::asio::use_awaitable);
    auto select_statement = co_await connection->async_prepare_statement(claim_singer_songs_select_sql_, boost::asio::use_awaitable);
    const auto statuses = claimStatusSlots(query.statuses, query.force);
    const int gender = optionalOrMinusOne(query.gender);
    const std::string claim_token = makeClaimToken("singer-songs");
    application::model::MiguBackfillClaimResult claim;

    const auto claim_targets = [&](const std::string &source_id, int limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(
            update_statement.bind(claim_token, std::string(kClaimOwner), query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.force ? 1 : 0, query.keyword, query.keyword, query.keyword, query.country, query.country, gender, gender, static_cast<std::uint64_t>(limit)),
            result,
            boost::asio::use_awaitable
        );
        co_return static_cast<int>(result.affected_rows());
    };

    if (query.source_ids.empty()) {
        claim.requested_count = co_await claim_targets("", query.limit);
    } else {
        claim.requested_count = static_cast<int>(query.source_ids.size());
        for (const auto &source_id : query.source_ids) {
            (void)co_await claim_targets(source_id, 1);
        }
    }

    boost::mysql::results result;
    co_await connection->async_execute(select_statement.bind(query.source, claim_token), result, boost::asio::use_awaitable);
    claim.singers.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        claim.singers.push_back(mapSinger(row));
    }
    finalizeClaimCounts(claim, query);
    co_return claim;
}

boost::asio::awaitable<application::model::MiguBackfillClaimResult> MySqlMusicCatalogQueryRepository::coClaimSingerAlbums(application::model::MiguBackfillTargetQuery query)
{
    query = normalizeClaimQuery(std::move(query));
    auto connection = co_await pool_->coAcquire();
    auto update_statement = co_await connection->async_prepare_statement(claim_singer_albums_update_sql_, boost::asio::use_awaitable);
    auto select_statement = co_await connection->async_prepare_statement(claim_singer_albums_select_sql_, boost::asio::use_awaitable);
    const auto statuses = claimStatusSlots(query.statuses, query.force);
    const int gender = optionalOrMinusOne(query.gender);
    const std::string claim_token = makeClaimToken("singer-albums");
    application::model::MiguBackfillClaimResult claim;

    const auto claim_targets = [&](const std::string &source_id, int limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(
            update_statement.bind(claim_token, std::string(kClaimOwner), query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.force ? 1 : 0, query.keyword, query.keyword, query.keyword, query.country, query.country, gender, gender, static_cast<std::uint64_t>(limit)),
            result,
            boost::asio::use_awaitable
        );
        co_return static_cast<int>(result.affected_rows());
    };

    if (query.source_ids.empty()) {
        claim.requested_count = co_await claim_targets("", query.limit);
    } else {
        claim.requested_count = static_cast<int>(query.source_ids.size());
        for (const auto &source_id : query.source_ids) {
            (void)co_await claim_targets(source_id, 1);
        }
    }

    boost::mysql::results result;
    co_await connection->async_execute(select_statement.bind(query.source, claim_token), result, boost::asio::use_awaitable);
    claim.singers.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        claim.singers.push_back(mapSinger(row));
    }
    finalizeClaimCounts(claim, query);
    co_return claim;
}

boost::asio::awaitable<application::model::MiguBackfillClaimResult> MySqlMusicCatalogQueryRepository::coClaimAlbumDetails(application::model::MiguBackfillTargetQuery query)
{
    query = normalizeClaimQuery(std::move(query));
    auto connection = co_await pool_->coAcquire();
    auto update_statement = co_await connection->async_prepare_statement(claim_album_details_update_sql_, boost::asio::use_awaitable);
    auto select_statement = co_await connection->async_prepare_statement(claim_album_details_select_sql_, boost::asio::use_awaitable);
    const auto statuses = claimStatusSlots(query.statuses, query.force);
    const int kind = optionalOrMinusOne(query.album_kind);
    const std::string claim_token = makeClaimToken("album-details");
    application::model::MiguBackfillClaimResult claim;

    const auto claim_targets = [&](const std::string &source_id, int limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(
            update_statement.bind(claim_token, std::string(kClaimOwner), query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, kind, kind, static_cast<std::uint64_t>(limit)),
            result,
            boost::asio::use_awaitable
        );
        co_return static_cast<int>(result.affected_rows());
    };

    if (query.source_ids.empty()) {
        claim.requested_count = co_await claim_targets("", query.limit);
    } else {
        claim.requested_count = static_cast<int>(query.source_ids.size());
        for (const auto &source_id : query.source_ids) {
            (void)co_await claim_targets(source_id, 1);
        }
    }

    boost::mysql::results result;
    co_await connection->async_execute(select_statement.bind(query.source, claim_token), result, boost::asio::use_awaitable);
    claim.albums.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        claim.albums.push_back(mapAlbum(row));
    }
    finalizeClaimCounts(claim, query);
    co_return claim;
}

boost::asio::awaitable<application::model::MiguBackfillClaimResult> MySqlMusicCatalogQueryRepository::coClaimAlbumSongs(application::model::MiguBackfillTargetQuery query)
{
    query = normalizeClaimQuery(std::move(query));
    auto connection = co_await pool_->coAcquire();
    auto update_statement = co_await connection->async_prepare_statement(claim_album_songs_update_sql_, boost::asio::use_awaitable);
    auto select_statement = co_await connection->async_prepare_statement(claim_album_songs_select_sql_, boost::asio::use_awaitable);
    const auto statuses = claimStatusSlots(query.statuses, query.force);
    const int kind = optionalOrMinusOne(query.album_kind);
    const std::string claim_token = makeClaimToken("album-songs");
    application::model::MiguBackfillClaimResult claim;

    const auto claim_targets = [&](const std::string &source_id, int limit) -> boost::asio::awaitable<int> {
        boost::mysql::results result;
        co_await connection->async_execute(
            update_statement.bind(claim_token, std::string(kClaimOwner), query.source, source_id, source_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], query.keyword, query.keyword, query.keyword, query.artist, query.artist, query.artist, kind, kind, static_cast<std::uint64_t>(limit)),
            result,
            boost::asio::use_awaitable
        );
        co_return static_cast<int>(result.affected_rows());
    };

    if (query.source_ids.empty()) {
        claim.requested_count = co_await claim_targets("", query.limit);
    } else {
        claim.requested_count = static_cast<int>(query.source_ids.size());
        for (const auto &source_id : query.source_ids) {
            (void)co_await claim_targets(source_id, 1);
        }
    }

    boost::mysql::results result;
    co_await connection->async_execute(select_statement.bind(query.source, claim_token), result, boost::asio::use_awaitable);
    claim.albums.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        claim.albums.push_back(mapAlbum(row));
    }
    finalizeClaimCounts(claim, query);
    co_return claim;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
