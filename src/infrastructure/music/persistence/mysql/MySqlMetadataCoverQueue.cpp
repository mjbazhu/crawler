#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"

#include "MySqlMetadataCoverSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/statement.hpp>

#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace model = application::model;

// 在基础设施边界映射表级查询列，封面不会流入歌曲队列模型。
model::MetadataCoverRecord readCover(boost::mysql::row_view row, model::CoverOwner owner)
{
    model::MetadataCoverRecord cover;
    cover.owner = owner;
    cover.resource_id = MySqlValueReader::uint64At(row, 0);
    cover.source_ref_id = MySqlValueReader::uint64At(row, 1);
    cover.source = MySqlValueReader::stringAt(row, 2);
    cover.source_id = MySqlValueReader::stringAt(row, 3);
    cover.remote_url = MySqlValueReader::stringAt(row, 4);
    cover.file.local_path = MySqlValueReader::stringAt(row, 5);
    cover.file.size_bytes = MySqlValueReader::uint64At(row, 6);
    cover.file.checksum = MySqlValueReader::stringAt(row, 7);
    cover.file.status = MySqlValueReader::intAt(row, 8);
    cover.file.error_category = MySqlValueReader::stringAt(row, 9);
    cover.file.result_code = MySqlValueReader::stringAt(row, 10);
    cover.file.last_error = MySqlValueReader::stringAt(row, 11);
    cover.claim_generation = MySqlValueReader::uint64At(row, 12);
    cover.claimed_at = MySqlValueReader::uint64At(row, 13);
    cover.running_batch_id = MySqlValueReader::stringAt(row, 14);
    cover.cloud.receipt = {MySqlValueReader::stringAt(row, 15), MySqlValueReader::stringAt(row, 16), MySqlValueReader::uint64At(row, 17), MySqlValueReader::stringAt(row, 18)};
    cover.cloud.status = MySqlValueReader::intAt(row, 19);
    cover.cloud.uploaded_at = MySqlValueReader::uint64At(row, 20);
    cover.cloud.claim_generation = MySqlValueReader::uint64At(row, 21);
    cover.cloud.claimed_at = MySqlValueReader::uint64At(row, 22);
    cover.cloud.running_batch_id = MySqlValueReader::stringAt(row, 23);
    cover.cloud.error_category = MySqlValueReader::stringAt(row, 24);
    cover.cloud.result_code = MySqlValueReader::stringAt(row, 25);
    cover.cloud.last_error = MySqlValueReader::stringAt(row, 26);
    return cover;
}

// 页查询只在数据库执行器运行，文件可用性由读取 port 另外检查。
boost::asio::awaitable<model::MetadataCoverPage> listCovers(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverQuery query)
{
    if (query.page < 1 || query.page_size < 1 || query.page_size > 200) throw std::invalid_argument("invalid cover page");
    auto connection = co_await pool->coAcquire();
    auto count = co_await connection->async_prepare_statement(cover_detail::filtered(query.owner, query.cloud_candidates ? "cover_process_count" : "cover_count", query), boost::asio::use_awaitable);
    boost::mysql::results rows;
    if (query.cloud_candidates)
        co_await connection->async_execute(count.bind(query.source, query.cloud_backend, query.cloud_prefix), rows, boost::asio::use_awaitable);
    else
        co_await connection->async_execute(count.bind(query.source), rows, boost::asio::use_awaitable);
    model::MetadataCoverPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    page.total = MySqlValueReader::uint64At(rows.rows().front(), 0);
    auto select = co_await connection->async_prepare_statement(cover_detail::filtered(query.owner, query.cloud_candidates ? "cover_process_list" : "cover_list", query), boost::asio::use_awaitable);
    const auto offset = static_cast<std::uint64_t>(query.page - 1) * static_cast<std::uint64_t>(query.page_size);
    if (query.cloud_candidates)
        co_await connection->async_execute(select.bind(query.source, query.cloud_backend, query.cloud_prefix, static_cast<std::uint64_t>(query.page_size), offset), rows, boost::asio::use_awaitable);
    else
        co_await connection->async_execute(select.bind(query.source, static_cast<std::uint64_t>(query.page_size), offset), rows, boost::asio::use_awaitable);
    for (auto row : rows.rows()) page.items.push_back(readCover(row, query.owner));
    co_return page;
}

// 更新与 URL、代次和状态一起比较，不需要持锁发出图片请求。
boost::asio::awaitable<std::optional<model::MetadataCoverRecord>> claimCover(std::shared_ptr<MySqlConnectionPool> pool, model::MetadataCoverRecord target, bool refresh, std::string batch)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(cover_detail::load(target.owner, "cover_claim"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(batch, target.resource_id, target.claim_generation, target.remote_url, refresh ? 1 : 0), result, boost::asio::use_awaitable);
    if (result.affected_rows() == 0) co_return std::nullopt;
    ++target.claim_generation;
    target.file.status = 1;
    target.running_batch_id = std::move(batch);
    co_return target;
}

} // namespace

MySqlMetadataCoverQueue::MySqlMetadataCoverQueue(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("metadata cover pool is required");
}

application::result::ServiceResult<application::model::MetadataCoverPage> MySqlMetadataCoverQueue::list(const application::model::MetadataCoverQuery &query)
{
    return cover_detail::run(pool_, listCovers(pool_, query));
}

application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> MySqlMetadataCoverQueue::find(application::model::CoverOwner owner, std::uint64_t id)
{
    using Result = application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>>;
    application::model::MetadataCoverQuery query;
    query.owner = owner;
    query.resource_ids = {id};
    query.page_size = 1;
    auto page = list(query);
    if (!page.isSuccess()) return Result::failure(page.error(), page.message());
    return Result::success(page.data()->items.empty() ? std::nullopt : std::optional(page.data()->items.front()));
}

application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> MySqlMetadataCoverQueue::claim(const application::model::MetadataCoverRecord &target, bool refresh, const std::string &batch_id)
{
    return cover_detail::run(pool_, claimCover(pool_, target, refresh, batch_id));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
