#include "MySqlSongCloudWriteback.h"

#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::song_writeback {
namespace {
namespace jobs = transfer_jobs;
using Update = application::model::SongResourceSuccessUpdate;
// 仅真实filesystem字节的完整SHA可绑定本地/参考代次，大小或其他算法指纹不升级。
std::string localSha(const Update &update)
{
    if (update.remote_only || update.size_bytes == 0 || update.checksum.size() != 64 || !std::all_of(update.checksum.begin(), update.checksum.end(), [](unsigned char c) { return std::isxdigit(c); })) return {};
    auto result = update.checksum;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
// 锁读取使用相同字段顺序，保留原生认领与完整字节快照围栏。
bool localMatches(boost::mysql::row_view row, const Update &update)
{
    return MySqlValueReader::intAt(row, 0) == 2 && MySqlValueReader::uint64At(row, 1) == update.claim_generation && MySqlValueReader::stringAt(row, 2) == update.local_path && MySqlValueReader::uint64At(row, 3) == update.size_bytes && MySqlValueReader::stringAt(row, 4) == update.checksum;
}
// 同一原生认领和工具报告可幂等确认，不重写云回执时间戳。
bool cloudMatches(boost::mysql::row_view row, const Update &update)
{
    return MySqlValueReader::stringAt(row, 0) == "succeeded" && MySqlValueReader::stringAt(row, 7) == update.cloud_backend && MySqlValueReader::stringAt(row, 8) == update.cloud_path && MySqlValueReader::uint64At(row, 9) == update.size_bytes && MySqlValueReader::stringAt(row, 10) == std::to_string(update.claim_generation) && MySqlValueReader::stringAt(row, 11) == update.local_path && MySqlValueReader::stringAt(row, 12) == update.checksum;
}
} // namespace

std::optional<std::string> provider(const Update &update, const std::string &configured)
{
    if (update.cloud_backend.empty()) return update.cloud_path.empty() ? std::optional<std::string>("") : std::nullopt;
    if (update.cloud_path.empty() || update.size_bytes == 0) return std::nullopt;
    if (update.cloud_backend == "baidupcs") return configured.empty() || configured == "baidu" ? std::optional<std::string>("baidu") : std::nullopt;
    if (update.cloud_backend == "rclone" && (configured == "baidu" || configured == "gdrive")) return configured;
    return std::nullopt;
}

boost::asio::awaitable<bool> persist(std::shared_ptr<MySqlConnectionPool> pool, Update update, std::string provider, std::string local_sql)
{
    auto work = [update = std::move(update), provider = std::move(provider), local_sql = std::move(local_sql)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        const auto source = co_await jobs::query(connection, "song_resources/legacy_success_lock", update.resource_id);
        if (source.rows().empty()) co_return false;
        const auto row = source.rows().front();
        const auto generation = MySqlValueReader::uint64At(row, 5);
        const bool local_done = localMatches(row, update);
        if (MySqlValueReader::intAt(row, 12)) co_return false;
        if (MySqlValueReader::uint64At(row, 1) != update.claim_generation || (MySqlValueReader::intAt(row, 0) != 1 && !local_done)) co_return false;
        const auto targets = co_await jobs::query(connection, "resource_destination_states/lock_all");
        std::map<std::string, std::uint64_t> revisions;
        for (const auto value : targets.rows()) revisions.emplace(MySqlValueReader::stringAt(value, 0), MySqlValueReader::uint64At(value, 1));
        if (!revisions.contains("local") || (!provider.empty() && !revisions.contains(provider))) co_return false;
        if (MySqlValueReader::uint64At(row, 6) == generation && MySqlValueReader::uint64At(row, 7) == revisions.at("local") && !MySqlValueReader::stringAt(row, 8).empty() && !MySqlValueReader::stringAt(row, 9).empty()) co_return false;
        const auto slots = co_await jobs::query(connection, "song_resources/legacy_success_slots", update.resource_id);
        if (!slots.rows().empty()) co_return false;
        const auto sha = localSha(update), reference = MySqlValueReader::stringAt(row, 10);
        if (!sha.empty() && ((!reference.empty() && reference != sha) || (MySqlValueReader::uint64At(row, 11) != 0 && MySqlValueReader::uint64At(row, 11) != update.size_bytes))) co_return false;
        bool cloud_done = false;
        if (!provider.empty()) {
            const auto cloud = co_await jobs::query(connection, "resource_cloud_receipts/legacy_song_lock", update.resource_id, provider);
            if (!cloud.rows().empty()) {
                const auto value = cloud.rows().front();
                if (MySqlValueReader::stringAt(value, 0) == "running" || MySqlValueReader::stringAt(value, 0) == "uncertain" || MySqlValueReader::intAt(value, 5) || !MySqlValueReader::stringAt(value, 6).empty() || MySqlValueReader::uint64At(value, 4) || MySqlValueReader::stringAt(value, 13) == "sha256_verified" || MySqlValueReader::stringAt(value, 13) == "hash_matched") co_return false;
                if (MySqlValueReader::stringAt(value, 1) == "skip" && MySqlValueReader::uint64At(value, 2) == generation && MySqlValueReader::uint64At(value, 3) == revisions.at(provider)) co_return false;
                cloud_done = cloudMatches(value, update) && MySqlValueReader::uint64At(value, 14) == generation && MySqlValueReader::uint64At(value, 15) == revisions.at(provider);
            }
        }
        if (!local_done) {
            auto statement = co_await connection->async_prepare_statement(local_sql, boost::asio::use_awaitable);
            boost::mysql::results changed;
            co_await connection->async_execute(statement.bind(update.local_path, update.size_bytes, update.checksum, update.result_code, update.batch_id, update.resource_id, update.claim_generation), changed, boost::asio::use_awaitable);
            if (changed.affected_rows() != 1) throw std::logic_error("native resource claim changed");
            if (!sha.empty()) (void)co_await jobs::query(connection, "song_resources/legacy_success_reference", sha, update.size_bytes, revisions.at("local"), update.resource_id, update.claim_generation);
        }
        if (!provider.empty() && !cloud_done) {
            const auto evidence = nlohmann::json {{"legacy_song_writeback", {{"claim_generation", update.claim_generation}, {"local_path", update.local_path}, {"checksum", update.checksum}, {"remote_only", update.remote_only}, {"basis", "legacy_adapter_report"}}}}.dump();
            (void)co_await jobs::query(connection, "resource_cloud_receipts/legacy_song_report", update.resource_id, provider, update.resource_id, generation, revisions.at(provider), update.cloud_path, update.size_bytes, update.cloud_backend, evidence);
        }
        co_return true;
    };
    co_return co_await jobs::transaction<bool>(std::move(pool), std::move(work), true);
}

boost::asio::awaitable<bool> inspect(std::shared_ptr<MySqlConnectionPool> pool, Update update, std::string provider)
{
    auto connection = co_await pool->coAcquire();
    const auto source = co_await jobs::query(connection, "song_resources/legacy_success_lock", update.resource_id);
    if (source.rows().empty() || !localMatches(source.rows().front(), update)) co_return false;
    if (provider.empty()) co_return true;
    const auto cloud = co_await jobs::query(connection, "resource_cloud_receipts/legacy_song_lock", update.resource_id, provider);
    co_return !cloud.rows().empty() && cloudMatches(cloud.rows().front(), update);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::song_writeback
