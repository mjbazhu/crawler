#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferAccess.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace model = application::model;
namespace ports = application::ports;
namespace result = application::result;

// 表名仅由固定枚举选择，不接收客户端 SQL 标识符。
std::string table(model::ResourceOwner owner)
{
    switch (owner) {
    case model::ResourceOwner::Song: return "song_resources";
    case model::ResourceOwner::Singer: return "singer_resources";
    case model::ResourceOwner::Album: return "album_resources";
    case model::ResourceOwner::SongArtifact: return "song_resource_artifacts";
    }
    throw std::invalid_argument("invalid resource owner");
}

// 准入只读查询有界等待，错误不回显数据库或路径。
template <typename T>
result::ServiceResult<T> wait(std::shared_ptr<MySqlConnectionPool> pool, boost::asio::awaitable<T> task)
{
    try {
        auto future = boost::asio::co_spawn(pool->executor(), std::move(task), boost::asio::use_future);
        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) return result::ServiceResult<T>::success(future.get());
    } catch (const std::invalid_argument &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::InvalidParam, "invalid resource admission identity");
    } catch (const std::exception &) {
        // 上层必须失败关闭，不能把数据库异常解释为允许执行。
    }
    return result::ServiceResult<T>::failure(result::ServiceErrorType::InternalError, "resource admission is unavailable");
}

// 单条联表查询取得同一快照，策略判断先于任何内容操作。
boost::asio::awaitable<std::optional<ports::ResourceTransferAdmission>> inspect(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceIdentity identity, model::ResourceDestination destination)
{
    if (!identity.resource_id || model::resourceOwnerName(identity.owner).empty() || model::resourceDestinationName(destination).empty())
        throw std::invalid_argument("invalid resource admission identity");
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/" + table(identity.owner) + "/transfer_admission.sql"), boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(std::string(model::resourceDestinationName(destination)), identity.resource_id), rows, boost::asio::use_awaitable);
    if (rows.rows().empty()) co_return std::nullopt;
    const auto row = rows.rows().front();
    ports::ResourceTransferAdmission value;
    value.version = {MySqlValueReader::uint64At(row, 0), MySqlValueReader::uint64At(row, 1)};
    value.skip.version = {MySqlValueReader::uint64At(row, 2), MySqlValueReader::uint64At(row, 3)};
    value.skip.baseline_id = MySqlValueReader::stringAt(row, 4);
    value.skip.reason = MySqlValueReader::stringAt(row, 5);
    value.skip.enabled = MySqlValueReader::intAt(row, 6) != 0;
    const auto state = MySqlValueReader::stringAt(row, 7);
    value.dispatch_ready = state == "active" && MySqlValueReader::intAt(row, 8) != 0 && MySqlValueReader::intAt(row, 9) != 0;
    value.blocked_code = state == "maintenance" ? "resource_maintenance" : state == "paused" ? "destination_paused" :
                                                                                               "baseline_not_ready";
    value.restricted = MySqlValueReader::intAt(row, 10) == 4;
    if (MySqlValueReader::intAt(row, 11)) {
        value.dispatch_ready = false;
        value.blocked_code = "historical_scope_excluded";
    }
    if (!value.version.content_generation || !value.version.target_revision) value.dispatch_ready = false;
    co_return value;
}

// 查询持久总闸门，不因其中一条目标行缺失而开放写入。
boost::asio::awaitable<bool> writable(std::shared_ptr<MySqlConnectionPool> pool)
{
    const auto rows = co_await pool->coExecute(sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_destination_states/writes_allowed.sql"));
    co_return rows.rows().size() == 1 && MySqlValueReader::intAt(rows.rows().front(), 0) == 1;
}

// 批次尚未选出具体文件时先检查目标，避免为暂停 local 刷新 token。
boost::asio::awaitable<bool> ready(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination)
{
    if (model::resourceDestinationName(destination).empty()) throw std::invalid_argument("invalid destination");
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_destination_states/destination_ready.sql"), boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(std::string(model::resourceDestinationName(destination))), rows, boost::asio::use_awaitable);
    co_return rows.rows().size() == 1 && MySqlValueReader::intAt(rows.rows().front(), 0) == 1;
}
} // namespace

MySqlResourceTransferAccess::MySqlResourceTransferAccess(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("resource admission requires a pool");
}

application::result::ServiceResult<application::ports::ResourceTransferAdmission> MySqlResourceTransferAccess::inspect(application::model::ResourceIdentity identity, application::model::ResourceDestination destination)
{
    using Result = application::result::ServiceResult<application::ports::ResourceTransferAdmission>;
    const auto value = wait(pool_, mysql::inspect(pool_, identity, destination));
    if (!value.isSuccess()) return Result::failure(value.error(), value.message());
    if (!value.data()->has_value()) return Result::failure(application::result::ServiceErrorType::NotFound, "resource or destination not found");
    return Result::success(**value.data());
}

application::result::ServiceResult<bool> MySqlResourceTransferAccess::writesAllowed()
{
    // 写入口同时尊重正式 schema 检查，不能因目标行 active 而绕过缺失身份约束。
    if (!MySqlMusicCatalogQueryRepository(pool_).schemaStatus().ok)
        return application::result::ServiceResult<bool>::failure(application::result::ServiceErrorType::Conflict, "resource schema is not ready");
    return wait(pool_, writable(pool_));
}

application::result::ServiceResult<bool> MySqlResourceTransferAccess::destinationReady(application::model::ResourceDestination destination)
{
    return wait(pool_, ready(pool_, destination));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
