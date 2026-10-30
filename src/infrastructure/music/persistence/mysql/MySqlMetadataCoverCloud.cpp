#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"

#include "MySqlMetadataCoverSupport.h"
#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/statement.hpp>
#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace model = application::model;

// 认领只绑定显式平台；资源、目标和逐端回执在一个短事务中检查。
boost::asio::awaitable<std::optional<model::MetadataCoverRecord>> claimCloudFile(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, model::CloudFileReceipt planned, std::string batch, model::ResourceDestination provider)
{
    const std::string selected(model::resourceDestinationName(provider));
    if (target.cloud.provider != selected || !model::supportsResourceTransferAdapter(provider, planned.backend)) co_return std::nullopt;
    auto work = [target = std::move(target), planned = std::move(planned), batch = std::move(batch), selected](boost::mysql::pooled_connection &connection) mutable -> boost::asio::awaitable<std::optional<model::MetadataCoverRecord>> {
        auto lock = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cloud_lock"), boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(lock.bind(selected, target.resource_id), result, boost::asio::use_awaitable);
        if (result.rows().empty() || MySqlValueReader::uint64At(result.rows().front(), 0) != target.cloud.content_generation || MySqlValueReader::uint64At(result.rows().front(), 1) != target.cloud.target_revision || MySqlValueReader::uint64At(result.rows().front(), 2) != target.cloud.source_revision) co_return std::nullopt;
        auto ensure = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cloud_ensure"), boost::asio::use_awaitable);
        co_await connection->async_execute(ensure.bind(selected, target.resource_id), result, boost::asio::use_awaitable);
        auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cloud_claim"), boost::asio::use_awaitable);
        const auto intent = nlohmann::json {{"legacy_cover", true}, {"provider", selected}, {"owner_type", model::coverOwnerName(target.owner)}, {"resource_id", target.resource_id}, {"local_path", target.file.local_path}, {"source_revision", target.cloud.source_revision}, {"local_claim_generation", target.claim_generation}, {"size_bytes", target.file.size_bytes}, {"checksum", target.file.checksum}, {"backend", planned.backend}, {"cloud_path", planned.path}}.dump();
        co_await connection->async_execute(statement.bind(batch, intent, selected, target.resource_id, target.cloud.claim_generation, target.cloud.content_generation, target.cloud.source_revision, target.cloud.target_revision, target.claim_generation, target.file.local_path, target.file.size_bytes, target.file.checksum, planned.path, planned.backend), result, boost::asio::use_awaitable);
        if (!result.affected_rows()) co_return std::nullopt;
        ++target.cloud.claim_generation;
        target.cloud.status = 1;
        target.cloud.running_batch_id = batch;
        co_return target;
    };
    co_return co_await transfer_jobs::transaction<std::optional<model::MetadataCoverRecord>>(std::move(pool), std::move(work), true);
}

// 云端结束只修改本平台回执，完整内容/目标/本地认领围栏拒绝晚到回写。
boost::asio::awaitable<bool> finishCloudFile(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, model::CloudFileResult upload, model::ResourceDestination provider)
{
    const std::string selected(model::resourceDestinationName(provider));
    if (target.cloud.provider != selected) co_return false;
    auto work = [target = std::move(target), upload = std::move(upload), selected](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        auto lock = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cloud_lock"), boost::asio::use_awaitable);
        boost::mysql::results locked;
        co_await connection->async_execute(lock.bind(selected, target.resource_id), locked, boost::asio::use_awaitable);
        if (locked.rows().empty() || MySqlValueReader::uint64At(locked.rows().front(), 0) != target.cloud.content_generation || MySqlValueReader::uint64At(locked.rows().front(), 1) != target.cloud.target_revision || MySqlValueReader::uint64At(locked.rows().front(), 2) != target.cloud.source_revision) co_return false;
        auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, upload.ok ? "cloud_succeed" : "cloud_fail"), boost::asio::use_awaitable);
        boost::mysql::results result;
        if (upload.ok) {
            const auto &file = upload.receipt;
            if (file.checksum != target.file.checksum || file.size_bytes != target.file.size_bytes) co_return false;
            co_await connection->async_execute(statement.bind(file.backend, file.path, file.size_bytes, file.checksum, file.checksum, file.size_bytes, selected, target.resource_id, target.cloud.claim_generation, target.cloud.running_batch_id, target.cloud.content_generation, target.cloud.source_revision, target.cloud.target_revision, target.claim_generation, target.file.local_path, target.file.size_bytes, target.file.checksum, file.path, file.backend), result, boost::asio::use_awaitable);
        } else {
            co_await connection->async_execute(statement.bind(upload.uncertain ? "uncertain" : "failed", upload.error_category, upload.result_code, "cover cloud operation was not confirmed", upload.uncertain ? 1 : 0, selected, target.resource_id, target.cloud.claim_generation, target.cloud.running_batch_id, target.cloud.content_generation, target.cloud.source_revision, target.cloud.target_revision), result, boost::asio::use_awaitable);
        }
        co_return result.affected_rows() != 0;
    };
    co_return co_await transfer_jobs::transaction<bool>(std::move(pool), std::move(work), true);
}

// 只回收独立云端认领，普通目录更新时间不会延长它的生命周期。
boost::asio::awaitable<int> recoverCloudFiles(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverQuery query, int stale_seconds, model::ResourceDestination provider)
{
    if (stale_seconds < 1) throw std::invalid_argument("cloud stale interval must be positive");
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::filtered(query.owner, "cloud_recover", query, false), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::string(model::resourceDestinationName(provider)), query.source, stale_seconds), result, boost::asio::use_awaitable);
    co_return static_cast<int>(result.affected_rows());
}
} // namespace

application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> MySqlMetadataCoverQueue::claimCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &planned, const std::string &batch_id)
{
    return cover_detail::run(pool_, claimCloudFile(pool_, target, planned, batch_id, cloud_provider_));
}

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::succeedCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &receipt)
{
    application::model::CloudFileResult uploaded;
    uploaded.ok = true;
    uploaded.receipt = receipt;
    auto result = cover_detail::run(pool_, finishCloudFile(pool_, target, uploaded, cloud_provider_));
    if (result.isSuccess()) return result;
    // 响应丢失后按已确认文件重读，不能把断线当作明确未提交。
    auto current = find(target.owner, target.resource_id);
    if (current.isSuccess() && current.data()->has_value()) {
        const auto &record = **current.data();
        if (record.cloud.provider == target.cloud.provider && record.cloud.content_generation == target.cloud.content_generation && record.cloud.source_revision == target.cloud.source_revision && record.cloud.target_revision == target.cloud.target_revision && record.file.status == 2 && record.cloud.status == 2 && record.file.local_path == target.file.local_path && record.file.size_bytes == target.file.size_bytes && record.file.checksum == target.file.checksum && record.cloud.receipt.backend == receipt.backend && record.cloud.receipt.path == receipt.path && record.cloud.receipt.checksum == receipt.checksum && record.cloud.receipt.size_bytes == receipt.size_bytes) return application::result::ServiceResult<bool>::success(true);
    }
    // 响应失败而未读到成功时，继续保留未知，不能伪装成明确的代次拒绝。
    return result;
}

application::result::ServiceResult<bool> MySqlMetadataCoverQueue::failCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileResult &error)
{
    return cover_detail::run(pool_, finishCloudFile(pool_, target, error, cloud_provider_));
}

application::result::ServiceResult<int> MySqlMetadataCoverQueue::recoverCloud(const application::model::MetadataCoverQuery &query, int stale_seconds)
{
    return cover_detail::run(pool_, recoverCloudFiles(pool_, query, stale_seconds, cloud_provider_));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
