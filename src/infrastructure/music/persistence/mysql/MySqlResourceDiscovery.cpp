#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include "MySqlResourceDiscoverySupport.h"
#include "MySqlLegacyResourceImportSupport.h"
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
namespace discovery {
boost::asio::awaitable<std::optional<model::ResourceDiscoveryRun>> read(boost::mysql::pooled_connection &connection, const std::string &id, bool lock)
{
    if (id.empty() || id.size() > 96) throw std::invalid_argument("invalid discovery id");
    auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_discovery_runs/read.sql");
    if (lock) text += " FOR UPDATE";
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(id), rows, boost::asio::use_awaitable);
    if (rows.rows().empty()) co_return std::nullopt;
    const auto row = rows.rows().front();
    model::ResourceDiscoveryRun value;
    const auto definition = nlohmann::json::parse(MySqlValueReader::stringAt(row, 1));
    value.definition = definitionFromJson(definition);
    value.definition_hash = MySqlValueReader::stringAt(row, 0);
    if (value.definition.run_id != id || digest(definition) != value.definition_hash || model::resourceOwnerName(value.definition.owner) != MySqlValueReader::stringAt(row, 9) || model::resourceDestinationName(value.definition.destination) != MySqlValueReader::stringAt(row, 10) || value.definition.high_water_id != MySqlValueReader::uint64At(row, 11)) throw std::logic_error("discovery definition changed");
    value.state = MySqlValueReader::stringAt(row, 2);
    value.cursor = MySqlValueReader::uint64At(row, 3);
    value.claim_generation = MySqlValueReader::uint64At(row, 4);
    value.lease_until = MySqlValueReader::uint64At(row, 5);
    value.processed = MySqlValueReader::uint64At(row, 6);
    value.counts = nlohmann::json::parse(MySqlValueReader::stringAt(row, 7)).get<std::map<std::string, std::uint64_t>>();
    value.result_code = MySqlValueReader::stringAt(row, 8);
    const auto known = emptyCounts();
    std::uint64_t processed = 0;
    if (value.counts.size() != known.size()) throw std::logic_error("invalid discovery counts");
    for (const auto &[grade, count] : value.counts) {
        if (!known.contains(grade)) throw std::logic_error("invalid discovery grade");
        processed += count;
    }
    if (processed != value.processed) throw std::logic_error("discovery count mismatch");
    co_return value;
}
bool owns(const model::ResourceDiscoveryRun &actual, const model::ResourceDiscoveryRun &lease)
{
    return actual.definition_hash == lease.definition_hash && actual.definition.run_id == lease.definition.run_id && actual.state == "running" && actual.claim_generation == lease.claim_generation && actual.cursor == lease.cursor && actual.lease_until == lease.lease_until && actual.lease_until > jobs::now();
}
boost::asio::awaitable<void> target(boost::mysql::pooled_connection &connection, const model::ResourceDiscoveryDefinition &d, bool lock)
{
    if (co_await legacy_import::databaseIdentity(connection) != d.database_identity) throw std::logic_error("discovery database changed");
    const auto states = co_await legacy_import::targets(connection, lock);
    const auto &target = states.at(static_cast<std::size_t>(d.destination));
    if (!d.target_revision || !jobs::hash(d.target_fingerprint) || target.revision != d.target_revision || target.fingerprint != d.target_fingerprint) throw std::logic_error("discovery target changed");
}
} // namespace discovery
MySqlResourceDiscovery::MySqlResourceDiscovery(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("discovery pool required");
}
application::result::ServiceResult<model::ResourceDiscoveryRun> MySqlResourceDiscovery::start(model::ResourceDiscoveryDefinition definition)
{
    auto work = [definition = std::move(definition)](boost::mysql::pooled_connection &connection) mutable -> boost::asio::awaitable<model::ResourceDiscoveryRun> {
        model::validateResourceDiscoveryDefinition(definition);
        if (!jobs::hash(definition.database_identity)) throw std::invalid_argument("discovery scope unresolved");
        co_await discovery::target(connection, definition, true);
        const auto json = discovery::definitionJson(definition);
        const auto hash = discovery::digest(json);
        const auto existing = co_await discovery::read(connection, definition.run_id, true);
        if (existing) {
            if (existing->definition_hash != hash) throw std::logic_error("discovery id reused");
            co_return *existing;
        }
        const auto name = std::string(model::resourceDestinationName(definition.destination));
        if (definition.high_water_id && !(co_await jobs::query(connection, "resource_discovery_runs/active", name)).rows().empty()) throw std::logic_error("discovery target busy");
        (void)co_await jobs::query(connection, "resource_discovery_runs/insert", definition.run_id, hash, json.dump(), std::string(model::resourceOwnerName(definition.owner)), name, definition.high_water_id, definition.high_water_id ? std::string("running") : std::string("completed"), nlohmann::json(discovery::emptyCounts()).dump());
        co_return *(co_await discovery::read(connection, definition.run_id, false));
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceDiscoveryRun>(pool_, std::move(work)), true);
}
application::result::ServiceResult<model::ResourceDiscoveryRun> MySqlResourceDiscovery::find(std::string id)
{
    auto work = [id = std::move(id)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceDiscoveryRun> {const auto value=co_await discovery::read(connection,id,false);if(!value)throw jobs::MissingOperation();co_return *value; };
    return jobs::run(pool_, jobs::transaction<model::ResourceDiscoveryRun>(pool_, std::move(work)));
}
application::result::ServiceResult<std::optional<model::ResourceDiscoveryRun>> MySqlResourceDiscovery::claim(std::string id)
{
    auto work = [id = std::move(id)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::optional<model::ResourceDiscoveryRun>> {
        const auto initial = co_await discovery::read(connection, id, false);
        if (!initial) throw jobs::MissingOperation();
        co_await discovery::target(connection, initial->definition, true);
        auto value = co_await discovery::read(connection, id, true);
        if (value->state != "running" || value->lease_until > jobs::now()) co_return std::nullopt;
        const auto changed = co_await jobs::query(connection, "resource_discovery_runs/claim", static_cast<std::uint64_t>((model::resourceDiscoveryPageBudget(value->definition) + 999) / 1000 + 60), id);
        if (changed.affected_rows() != 1) co_return std::nullopt;
        co_return co_await discovery::read(connection, id, false);
    };
    return jobs::run(pool_, jobs::transaction<std::optional<model::ResourceDiscoveryRun>>(pool_, std::move(work), true), true);
}
application::result::ServiceResult<bool> MySqlResourceDiscovery::current(const model::ResourceDiscoveryRun &lease)
{
    auto work = [lease](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {const auto actual=co_await discovery::read(connection,lease.definition.run_id,false);if(!actual || !discovery::owns(*actual,lease))co_return false;co_await discovery::target(connection,lease.definition,false);co_return true; };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
