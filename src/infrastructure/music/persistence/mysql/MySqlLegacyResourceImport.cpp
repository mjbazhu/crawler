#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"

#include "MySqlLegacyResourceImportSupport.h"

#include <boost/asio/use_awaitable.hpp>

#include <limits>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import {

boost::asio::awaitable<std::string> databaseIdentity(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    co_await connection->async_execute(load("schema/resource_database_identity"), result, boost::asio::use_awaitable);
    if (result.rows().empty()) throw std::runtime_error("resource database identity is missing");
    co_return MySqlValueReader::stringAt(result.rows().front(), 0);
}

boost::asio::awaitable<std::array<model::LegacyResourceTarget, 3>> targets(boost::mysql::pooled_connection &connection, bool lock)
{
    boost::mysql::results result;
    co_await connection->async_execute(load(lock ? "resource_destination_states/lock_all" : "resource_destination_states/list"), result, boost::asio::use_awaitable);
    std::array<model::LegacyResourceTarget, 3> values;
    std::array<bool, 3> seen {};
    for (auto row : result.rows()) {
        const auto destination = model::parseResourceDestination(MySqlValueReader::stringAt(row, 0));
        if (!destination) throw std::logic_error("unknown resource destination");
        const auto index = static_cast<std::size_t>(*destination);
        values[index] = {*destination, MySqlValueReader::uint64At(row, 1), MySqlValueReader::stringAt(row, 2), MySqlValueReader::stringAt(row, 3)};
        seen[index] = true;
    }
    if (!std::all_of(seen.begin(), seen.end(), [](bool value) { return value; })) throw std::logic_error("resource destination state is incomplete");
    co_return values;
}

namespace {
// 从两个原子写入的控制行恢复游标，不锁定或扫描已经处理过的资源。
boost::asio::awaitable<model::LegacyResourceImportProgress> progress(std::shared_ptr<MySqlConnectionPool> pool, model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage page)
{
    if (plan.run_id.empty() || !hash(plan.manifest_hash) || !hash(page.database_identity)) throw std::invalid_argument("invalid import progress identity");
    auto connection = co_await pool->coAcquire();
    if (co_await databaseIdentity(connection) != page.database_identity) throw std::logic_error("wrong import database");
    auto statement = co_await connection->async_prepare_statement(load("resource_cloud_backfills/progress"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(plan.run_id + ":baidu", plan.run_id + ":gdrive"), result, boost::asio::use_awaitable);
    if (result.rows().empty()) co_return model::LegacyResourceImportProgress {};
    if (result.rows().size() != 2) throw std::logic_error("incomplete import cursor");
    std::optional<model::LegacyResourceImportProgress> status;
    for (auto row : result.rows()) {
        const auto provider = model::parseCloudProvider(MySqlValueReader::stringAt(row, 3));
        if (!provider || MySqlValueReader::stringAt(row, 0) != plan.manifest_hash || MySqlValueReader::stringAt(row, 1) != page.database_identity || MySqlValueReader::stringAt(row, 2) != model::resourceOwnerName(page.owner) || MySqlValueReader::uint64At(row, 4) != page.targets[static_cast<std::size_t>(*provider)].revision || MySqlValueReader::uint64At(row, 5) != page.high_water_id) throw std::logic_error("import progress plan changed");
        model::LegacyResourceImportProgress value {MySqlValueReader::uint64At(row, 6), MySqlValueReader::uint64At(row, 8), MySqlValueReader::uint64At(row, 9), MySqlValueReader::uint64At(row, 10), MySqlValueReader::stringAt(row, 7)};
        value.control_state = MySqlValueReader::stringAt(row, 11);
        value.control_generation = MySqlValueReader::uint64At(row, 12);
        if (MySqlValueReader::stringAt(row, 13) != "import_attested_skip") throw std::logic_error("import strategy changed");
        if (status && (status->cursor != value.cursor || status->processed_resources != value.processed_resources || status->imported_resources != value.imported_resources || status->conflicts != value.conflicts || status->state != value.state || status->control_state != value.control_state || status->control_generation != value.control_generation)) throw std::logic_error("provider progress diverged");
        status = std::move(value);
    }
    co_return *status;
}

// 只读有界预览，未锁定数据的变化在实际页事务中再次比较。
boost::asio::awaitable<model::LegacyResourceImportPage> readPage(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceOwner owner, std::uint64_t after, std::uint64_t high_water, std::size_t limit)
{
    if (!limit || limit > 5000) throw std::invalid_argument("invalid import page size");
    auto connection = co_await pool->coAcquire();
    model::LegacyResourceImportPage page;
    page.owner = owner;
    page.database_identity = co_await databaseIdentity(connection);
    page.targets = co_await targets(connection, false);
    if (!high_water) {
        boost::mysql::results maximum;
        co_await connection->async_execute(load(table(owner) + "/cloud_import_max_id"), maximum, boost::asio::use_awaitable);
        high_water = MySqlValueReader::uint64At(maximum.rows().front(), 0);
    }
    if (after > high_water) throw std::invalid_argument("invalid import cursor");
    page.after_id = after;
    page.high_water_id = high_water;
    auto statement = co_await connection->async_prepare_statement(load(table(owner) + "/cloud_import_snapshot"), boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(after, high_water, static_cast<std::uint64_t>(limit)), rows, boost::asio::use_awaitable);
    for (auto row : rows.rows()) page.rows.push_back(snapshot(owner, row));
    page.next_id = page.rows.size() < limit ? high_water : page.rows.back().identity.resource_id;
    co_return page;
}

// 单行配置比较更新；只在显式维护中接受操作者核实的目标指纹。
boost::asio::awaitable<std::uint64_t> configure(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, std::uint64_t expected, std::string fingerprint)
{
    if (!hash(fingerprint) || !expected || model::resourceDestinationName(destination).empty()) throw std::invalid_argument("invalid target identity");
    auto connection = co_await pool->coAcquire();
    auto current_statement = co_await connection->async_prepare_statement(load("resource_destination_states/find"), boost::asio::use_awaitable);
    boost::mysql::results rows;
    const std::string name(model::resourceDestinationName(destination));
    co_await connection->async_execute(current_statement.bind(name), rows, boost::asio::use_awaitable);
    if (rows.rows().size() != 1) throw std::logic_error("target is missing");
    const auto row = rows.rows().front();
    if (MySqlValueReader::uint64At(row, 0) != expected || MySqlValueReader::stringAt(row, 2) != "maintenance") throw std::logic_error("target changed or is not maintained");
    const auto previous = MySqlValueReader::stringAt(row, 1);
    if (previous == fingerprint) co_return expected;
    if (!previous.empty() && expected == (std::numeric_limits<std::uint64_t>::max)()) throw std::invalid_argument("target revision overflow");
    const auto revision = previous.empty() ? expected : expected + 1;
    auto statement = co_await connection->async_prepare_statement(load("resource_destination_states/configure"), boost::asio::use_awaitable);
    boost::mysql::results updated;
    bool acknowledged = false;
    try {
        co_await connection->async_execute(statement.bind(revision, fingerprint, name, expected, previous), updated, boost::asio::use_awaitable);
        acknowledged = true;
    } catch (const std::exception &) {
        // 单语句为 autocommit，断线后不能假定目标修订没有变化。
    }
    if (!acknowledged) throw CommitUnknown();
    if (updated.affected_rows() != 1) throw std::logic_error("target changed concurrently");
    co_return revision;
}
} // namespace
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

MySqlLegacyResourceImport::MySqlLegacyResourceImport(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("resource import requires a pool");
}

application::result::ServiceResult<application::model::LegacyResourceImportPage> MySqlLegacyResourceImport::preview(application::model::ResourceOwner owner, std::uint64_t after_id, std::uint64_t high_water_id, std::size_t limit)
{
    return legacy_import::run(pool_, legacy_import::readPage(pool_, owner, after_id, high_water_id, limit));
}

application::result::ServiceResult<application::model::LegacyResourceImportResult> MySqlLegacyResourceImport::importPage(application::model::LegacyResourceImportPlan plan, application::model::LegacyResourceImportPage page)
{
    return legacy_import::run(pool_, legacy_import::apply(pool_, std::move(plan), std::move(page)), true);
}

application::result::ServiceResult<std::uint64_t> MySqlLegacyResourceImport::configureTarget(application::model::ResourceDestination destination, std::uint64_t expected_revision, std::string fingerprint)
{
    return legacy_import::run(pool_, legacy_import::configure(pool_, destination, expected_revision, std::move(fingerprint)), true);
}

application::result::ServiceResult<application::model::LegacyResourceImportProgress> MySqlLegacyResourceImport::progress(application::model::LegacyResourceImportPlan plan, application::model::LegacyResourceImportPage scope)
{
    return legacy_import::run(pool_, legacy_import::progress(pool_, std::move(plan), std::move(scope)));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
