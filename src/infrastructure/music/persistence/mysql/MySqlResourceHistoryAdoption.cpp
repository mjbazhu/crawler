#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceHistoryAdoption.h"
#include "MySqlResourceHistorySupport.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
namespace history {
void validate(const model::ResourceHistoryAdoptionPlan &p)
{
    model::validateResourceHistoryAdoptionPlan(p);
}
std::string key(const model::ResourceHistoryAdoptionPlan &p)
{
    return p.run_id + ':' + std::string(model::resourceDestinationName(p.destination));
}
boost::asio::awaitable<model::ResourceHistoryAdoptionProgress> read(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &p, bool lock)
{
    auto text = legacy_import::load("resource_cloud_backfills/history_read");
    if (lock) text += " FOR UPDATE";
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(key(p)), rows, boost::asio::use_awaitable);
    model::ResourceHistoryAdoptionProgress value;
    if (rows.rows().empty()) co_return value;
    const auto row = rows.rows().front();
    if (MySqlValueReader::stringAt(row, 0) != p.manifest_hash || MySqlValueReader::stringAt(row, 1) != p.database_identity || MySqlValueReader::stringAt(row, 2) != model::resourceOwnerName(p.owner) || MySqlValueReader::stringAt(row, 3) != model::resourceDestinationName(p.destination) || MySqlValueReader::uint64At(row, 4) != p.target_revision || MySqlValueReader::uint64At(row, 5) != p.high_water_id || MySqlValueReader::stringAt(row, 13) != "import_discovery_skip") throw std::logic_error("history adoption definition changed");
    value.cursor = MySqlValueReader::uint64At(row, 6);
    value.state = MySqlValueReader::stringAt(row, 7);
    value.processed = MySqlValueReader::uint64At(row, 8);
    value.imported = MySqlValueReader::uint64At(row, 9);
    value.conflicts = MySqlValueReader::uint64At(row, 10);
    value.control_state = MySqlValueReader::stringAt(row, 11);
    value.control_generation = MySqlValueReader::uint64At(row, 12);
    if (value.processed > p.report_count || value.cursor > p.high_water_id) throw std::logic_error("history adoption progress corrupt");
    co_return value;
}
boost::asio::awaitable<void> scope(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &p, bool maintained)
{
    if (co_await legacy_import::databaseIdentity(connection) != p.database_identity) throw std::logic_error("history adoption database changed");
    const auto targets = co_await legacy_import::targets(connection, maintained);
    const auto &target = targets.at(static_cast<std::size_t>(p.destination));
    if (target.revision != p.target_revision || target.fingerprint != p.target_fingerprint) throw std::logic_error("history adoption target changed");
    if (maintained)
        for (const auto &t : targets)
            if (t.dispatch_state != "maintenance") throw std::logic_error("history adoption requires maintenance");
    const auto run = co_await discovery::read(connection, p.discovery_run_id, false);
    if (!run || run->definition_hash != p.discovery_definition_hash || run->definition.owner != p.owner || run->definition.destination != p.destination || run->definition.database_identity != p.database_identity || run->definition.target_revision != p.target_revision || run->definition.target_fingerprint != p.target_fingerprint || run->cursor < p.high_water_id || run->processed < p.report_count || (run->cursor == p.high_water_id && run->processed != p.report_count)) throw std::logic_error("history report scope changed");
}
} // namespace history
MySqlResourceHistoryAdoption::MySqlResourceHistoryAdoption(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("history adoption pool required");
}
application::result::ServiceResult<model::ResourceHistoryAdoptionProgress> MySqlResourceHistoryAdoption::progress(model::ResourceHistoryAdoptionPlan plan)
{
    auto work = [plan = std::move(plan)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceHistoryAdoptionProgress> {history::validate(plan);if(co_await legacy_import::databaseIdentity(connection)!=plan.database_identity)throw std::logic_error("history adoption database changed");co_return co_await history::read(connection,plan,false); };
    return jobs::run(pool_, jobs::transaction<model::ResourceHistoryAdoptionProgress>(pool_, std::move(work)));
}
application::result::ServiceResult<model::ResourceHistoryAdoptionProgress> MySqlResourceHistoryAdoption::control(model::ResourceHistoryAdoptionPlan plan, std::string action)
{
    auto work = [plan = std::move(plan), action = std::move(action)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceHistoryAdoptionProgress> {
        history::validate(plan);
        if (action != "pause" && action != "resume" && action != "cancel") throw std::invalid_argument("invalid history control");
        if (action == "resume")
            co_await history::scope(connection, plan, true);
        else
            (void)co_await jobs::targets(connection);
        if (co_await legacy_import::databaseIdentity(connection) != plan.database_identity) throw std::logic_error("history control database changed");
        const auto before = co_await history::read(connection, plan, true);
        if (before.state == "not_started" || before.control_generation != plan.control_generation) throw std::logic_error("history control generation changed");
        const std::string desired = action == "resume" ? "active" : action == "pause" ? "paused" :
                                                                                        "cancelled";
        if (before.state == "completed" || (action == "resume" && before.control_state != "paused") || (before.control_state == "cancelled" && desired != "cancelled")) throw std::logic_error("history control terminal");
        if (before.control_state == desired) co_return before;
        const auto changed = co_await jobs::query(connection, "resource_cloud_backfills/control", desired, history::key(plan), plan.manifest_hash, plan.control_generation);
        if (changed.affected_rows() != 1) throw std::logic_error("history control changed");
        const auto payload = nlohmann::json {{"manifest_sha256", plan.manifest_hash}, {"report_sha256", plan.report_hash}, {"control_generation", plan.control_generation + 1}, {"cursor", before.cursor}}.dump();
        (void)co_await jobs::query(connection, "resource_cloud_backfills/control_audit", plan.run_id, before.control_state, desired, payload);
        co_return co_await history::read(connection, plan, false);
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceHistoryAdoptionProgress>(pool_, std::move(work), true), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
