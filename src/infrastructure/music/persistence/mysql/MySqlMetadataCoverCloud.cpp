#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"

#include "MySqlMetadataCoverSupport.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/statement.hpp>
#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace model = application::model;

// 认领和上传意图同一 SQL 持久化，不把客户端配置或认证值写入日志。
boost::asio::awaitable<std::optional<model::MetadataCoverRecord>> claimCloudFile(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, model::CloudFileReceipt planned, std::string batch)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cloud_claim"), boost::asio::use_awaitable);
    const auto intent = nlohmann::json {{"owner_type", model::coverOwnerName(target.owner)}, {"resource_id", target.resource_id}, {"local_path", target.file.local_path}, {"size_bytes", target.file.size_bytes}, {"checksum", target.file.checksum}, {"backend", planned.backend}, {"cloud_path", planned.path}}.dump();
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(batch, intent, target.resource_id, target.cloud.claim_generation, target.file.local_path, target.file.size_bytes, target.file.checksum), result, boost::asio::use_awaitable);
    if (!result.affected_rows()) co_return std::nullopt;
    ++target.cloud.claim_generation;
    target.cloud.status = 1;
    target.cloud.running_batch_id = std::move(batch);
    co_return target;
}

// 云端确认只更新云端列；不得重置本地下载状态和文件回执。
boost::asio::awaitable<bool> finishCloudFile(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, model::CloudFileResult upload)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, upload.ok ? "cloud_succeed" : "cloud_fail"), boost::asio::use_awaitable);
    boost::mysql::results result;
    if (upload.ok) {
        const auto &file = upload.receipt;
        co_await connection->async_execute(statement.bind(file.backend, file.path, file.size_bytes, file.checksum, target.resource_id, target.cloud.claim_generation, target.file.local_path, target.file.size_bytes, target.file.checksum), result, boost::asio::use_awaitable);
    } else {
        co_await connection->async_execute(statement.bind(upload.uncertain ? 4 : 3, upload.error_category, upload.result_code, "cover cloud operation was not confirmed", target.resource_id, target.cloud.claim_generation), result, boost::asio::use_awaitable);
    }
    co_return result.affected_rows() != 0;
}

// 只回收独立云端认领，普通目录更新时间不会延长它的生命周期。
boost::asio::awaitable<int> recoverCloudFiles(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverQuery query, int stale_seconds)
{
    if (stale_seconds < 1) throw std::invalid_argument("cloud stale interval must be positive");
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::filtered(query.owner, "cloud_recover", query, false), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(query.source, stale_seconds), result, boost::asio::use_awaitable);
    co_return static_cast<int>(result.affected_rows());
}
} // namespace

application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> MySqlMetadataCoverQueue::claimCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &planned, const std::string &batch_id)
{
    return cover_detail::run(pool_, claimCloudFile(pool_, target, planned, batch_id));
}

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::succeedCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &receipt)
{
    application::model::CloudFileResult uploaded;
    uploaded.ok = true;
    uploaded.receipt = receipt;
    auto result = cover_detail::run(pool_, finishCloudFile(pool_, target, uploaded));
    if (result.isSuccess()) return result;
    // 响应丢失后按已确认文件重读，不能把断线当作明确未提交。
    auto current = find(target.owner, target.resource_id);
    if (!current.isSuccess()) return result;
    if (!*current.data()) return application::result::ServiceResult<bool>::success(false);
    const auto &record = **current.data();
    return application::result::ServiceResult<bool>::success(record.cloud.status == 2 && record.file.local_path == target.file.local_path && record.file.checksum == target.file.checksum && record.cloud.receipt.backend == receipt.backend && record.cloud.receipt.path == receipt.path && record.cloud.receipt.checksum == receipt.checksum && record.cloud.receipt.size_bytes == receipt.size_bytes);
}

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::failCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileResult &error)
{
    return cover_detail::run(pool_, finishCloudFile(pool_, target, error));
}

application::result::ServiceResult<int> MySqlMetadataCoverQueue::recoverCloud(const application::model::MetadataCoverQuery &query, int stale_seconds)
{
    return cover_detail::run(pool_, recoverCloudFiles(pool_, query, stale_seconds));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
