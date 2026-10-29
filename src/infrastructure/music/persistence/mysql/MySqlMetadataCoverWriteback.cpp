#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"

#include "MySqlMetadataCoverSupport.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/statement.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace model = application::model;

// 仅成功路径替换回执，失败路径保留上次完整图片。
boost::asio::awaitable<bool> finishCover(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, model::MetadataResourceFile file, bool success)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, success ? "cover_succeed" : "cover_fail"), boost::asio::use_awaitable);
    boost::mysql::results result;
    if (success) {
        co_await connection->async_execute(statement.bind(file.local_path, file.size_bytes, file.checksum, target.resource_id, target.claim_generation, target.remote_url), result, boost::asio::use_awaitable);
    } else {
        co_await connection->async_execute(statement.bind(file.error_category, file.result_code, file.last_error, target.resource_id, target.claim_generation), result, boost::asio::use_awaitable);
    }
    co_return result.affected_rows() != 0;
}

// 文件回执可能随专辑物料归一迁到另一资源主键，按路径核对是否仍被引用。
boost::asio::awaitable<bool> referencedCover(std::shared_ptr<MySqlConnectionPool> pool, model::CoverOwner owner, std::string path)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::load(owner, "cover_find_receipt"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(path), result, boost::asio::use_awaitable);
    co_return !result.rows().empty();
}

// claimed_at 独立于元数据 updated_at，防止目录重采使停滞任务永不超时。
boost::asio::awaitable<int> recoverCovers(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverQuery query, int stale_seconds)
{
    if (stale_seconds < 1) throw std::invalid_argument("cover stale interval must be positive");
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::filtered(query.owner, "cover_recover", query, false), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(query.source, stale_seconds), result, boost::asio::use_awaitable);
    co_return static_cast<int>(result.affected_rows());
}
} // namespace

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::succeed(const application::model::MetadataCoverRecord &target, const application::model::MetadataResourceFile &file)
{
    auto written = cover_detail::run(pool_, finishCover(pool_, target, file, true));
    if (written.isSuccess()) return written;
    // 断线不等于回滚；先按独占文件路径重新读取事实。
    auto current = find(target.owner, target.resource_id);
    if (!current.isSuccess()) return written;
    if (!*current.data()) return application::result::ServiceResult<bool>::success(false);
    const auto &receipt = (**current.data()).file;
    return application::result::ServiceResult<bool>::success(receipt.local_path == file.local_path && receipt.size_bytes == file.size_bytes && receipt.checksum == file.checksum);
}

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::fail(const application::model::MetadataCoverRecord &target, const application::model::MetadataResourceFile &error)
{
    return cover_detail::run(pool_, finishCover(pool_, target, error, false));
}

application::result::ServiceResult<int> MySqlMetadataCoverQueue::recover(const application::model::MetadataCoverQuery &query, int stale_seconds)
{
    return cover_detail::run(pool_, recoverCovers(pool_, query, stale_seconds));
}

std::optional<application::model::ResourceWriteDisposition> MySqlMetadataCoverQueue::inspectWrite(application::model::CoverOwner owner, std::uint64_t id, std::uint64_t generation, const std::string &path)
{
    using State = application::model::ResourceWriteDisposition;
    auto current = find(owner, id);
    if (!current.isSuccess()) return std::nullopt;
    if (*current.data() && (**current.data()).file.local_path == path) return State::Committed;
    // 专辑物料归一可能把成功回执迁到另一主键，不能按旧 ID 删除仍被引用的文件。
    const auto referenced = cover_detail::run(pool_, referencedCover(pool_, owner, path));
    if (!referenced.isSuccess()) return std::nullopt;
    if (*referenced.data()) return State::Committed;
    if (*current.data()) {
        const auto &record = **current.data();
        if (record.file.status == 1 && record.claim_generation == generation) return State::Active;
    }
    return State::Discard;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
