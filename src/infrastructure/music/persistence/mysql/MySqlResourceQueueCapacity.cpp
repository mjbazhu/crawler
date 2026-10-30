#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceQueueCapacity.h"
#include "MySqlResourceQueueCapacitySupport.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
namespace {
// 当前锁读与普通快照读共享相同外置查询。
boost::asio::awaitable<model::ResourceQueueCapacity> limits(boost::mysql::pooled_connection &connection, model::ResourceDestination destination, bool lock)
{
    if (model::resourceDestinationName(destination).empty()) throw std::invalid_argument("invalid capacity destination");
    auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_destination_states/queue_limits.sql");
    if (lock) text += " FOR UPDATE";
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(std::string(model::resourceDestinationName(destination))), rows, boost::asio::use_awaitable);
    if (rows.rows().size() != 1) throw std::logic_error("capacity destination missing");
    const auto row = rows.rows().front();
    model::ResourceQueueCapacity value;
    value.destination = destination;
    value.transfer_limit = MySqlValueReader::uint64At(row, 1);
    value.producer_limit = MySqlValueReader::uint64At(row, 2);
    value.version = MySqlValueReader::uint64At(row, 3);
    if (!value.transfer_limit || value.transfer_limit > 1000000 || !value.producer_limit || value.producer_limit > 1000000 || !value.version) throw std::logic_error("invalid persisted capacity");
    co_return value;
}
// 限额加一封顶；锁读在子查询锁定当前基表行，只回传一个整数。
boost::asio::awaitable<std::uint64_t> count(boost::mysql::pooled_connection &connection, std::string path, model::ResourceDestination destination, std::uint64_t maximum, bool lock)
{
    const auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/" + path + (lock ? "/queue_occupancy_count.sql" : "/queue_occupancy.sql"));
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(std::string(model::resourceDestinationName(destination)), maximum + 1), rows, boost::asio::use_awaitable);
    if (!lock) co_return rows.rows().size();
    if (rows.rows().size() != 1 || rows.rows().front().size() != 1) throw std::logic_error("invalid locked capacity count");
    const auto field = rows.rows().front().at(0);
    std::uint64_t value = 0;
    if (field.is_uint64())
        value = field.as_uint64();
    else if (field.is_int64() && field.as_int64() >= 0)
        value = static_cast<std::uint64_t>(field.as_int64());
    else
        throw std::logic_error("invalid locked capacity count type");
    if (value > maximum + 1) throw std::logic_error("unbounded locked capacity count");
    co_return value;
}
// 可按拟议限额读占用，用于安全调低或提高限额。
boost::asio::awaitable<void> usage(boost::mysql::pooled_connection &connection, model::ResourceQueueCapacity &value, bool lock)
{
    value.transfers = co_await count(connection, "resource_transfer_tasks", value.destination, value.transfer_limit, lock);
    value.producers = co_await count(connection, "resource_producer_tasks", value.destination, value.producer_limit, lock);
    value.transfers_exact = value.transfers <= value.transfer_limit;
    value.producers_exact = value.producers <= value.producer_limit;
}
} // namespace
namespace transfer_jobs {
boost::asio::awaitable<model::ResourceQueueCapacity> queueCapacity(boost::mysql::pooled_connection &connection, model::ResourceDestination destination, bool lock)
{
    auto value = co_await limits(connection, destination, lock);
    co_await usage(connection, value, lock);
    co_return value;
}
boost::asio::awaitable<void> enforceQueueCapacity(boost::mysql::pooled_connection &connection, model::ResourceDestination destination, bool transfers, bool producers)
{
    const auto value = co_await limits(connection, destination, true);
    if (transfers && co_await count(connection, "resource_transfer_tasks", destination, value.transfer_limit, true) > value.transfer_limit) throw QueueCapacityExceeded();
    if (producers && co_await count(connection, "resource_producer_tasks", destination, value.producer_limit, true) > value.producer_limit) throw QueueCapacityExceeded();
}
boost::asio::awaitable<void> enforceOperationCapacity(boost::mysql::pooled_connection &connection, const std::string &id)
{
    for (auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
        const auto name = std::string(model::resourceDestinationName(destination));
        const auto transfers = co_await query(connection, "resource_transfer_tasks/queue_new_work", id, name);
        const auto producers = co_await query(connection, "resource_producer_tasks/queue_new_work", id, name);
        const bool new_transfers = MySqlValueReader::uint64At(transfers.rows().front(), 0) > 0, new_producers = MySqlValueReader::uint64At(producers.rows().front(), 0) > 0;
        if (new_transfers || new_producers) co_await enforceQueueCapacity(connection, destination, new_transfers, new_producers);
    }
}
} // namespace transfer_jobs
MySqlResourceQueueCapacity::MySqlResourceQueueCapacity(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("capacity pool required");
}
application::result::ServiceResult<std::vector<model::ResourceQueueCapacity>> MySqlResourceQueueCapacity::status()
{
    auto work = [](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::vector<model::ResourceQueueCapacity>> {
        std::vector<model::ResourceQueueCapacity> values;
        for (auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) values.push_back(co_await jobs::queueCapacity(connection, destination, false));
        co_return values;
    };
    return jobs::run(pool_, jobs::transaction<std::vector<model::ResourceQueueCapacity>>(pool_, std::move(work)));
}
application::result::ServiceResult<model::ResourceQueueCapacity> MySqlResourceQueueCapacity::configure(model::ResourceDestination destination, std::uint64_t version, std::uint64_t transfers, std::uint64_t producers)
{
    auto work = [=](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceQueueCapacity> {
        if (!version || !transfers || transfers > 1000000 || !producers || producers > 1000000) throw std::invalid_argument("invalid queue limits");
        (void)co_await jobs::targets(connection);
        const auto before = co_await limits(connection, destination, true);
        if (before.version != version) throw std::logic_error("queue limits version changed");
        auto after = before;
        after.transfer_limit = transfers;
        after.producer_limit = producers;
        co_await usage(connection, after, true);
        if (after.transfers > transfers || after.producers > producers) throw std::logic_error("capacity below current occupancy");
        if (before.transfer_limit == transfers && before.producer_limit == producers) co_return after;
        const auto changed = co_await jobs::query(connection, "resource_destination_states/queue_configure", transfers, producers, std::string(model::resourceDestinationName(destination)), version);
        if (changed.affected_rows() != 1) throw std::logic_error("queue limits changed");
        after.version++;
        const auto payload = nlohmann::json {{"before", {{"transfers", before.transfer_limit}, {"producers", before.producer_limit}, {"version", before.version}}}, {"after", {{"transfers", after.transfer_limit}, {"producers", after.producer_limit}, {"version", after.version}}}}.dump();
        (void)co_await jobs::query(connection, "governance_events/queue_capacity", std::string(model::resourceDestinationName(destination)), payload);
        co_return after;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceQueueCapacity>(pool_, std::move(work)), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
