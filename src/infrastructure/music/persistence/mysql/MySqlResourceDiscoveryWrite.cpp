#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include "MySqlResourceDiscoverySupport.h"
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
application::result::ServiceResult<bool> MySqlResourceDiscovery::commit(const model::ResourceDiscoveryRun &lease, const model::LegacyResourceImportPage &page, std::vector<model::ResourceDiscoveryObservation> observations)
{
    auto work = [lease, page, observations = std::move(observations)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        co_await discovery::target(connection, lease.definition, true);
        const auto actual = co_await discovery::read(connection, lease.definition.run_id, true);
        if (!actual || !discovery::owns(*actual, lease)) co_return false;
        const auto &d = actual->definition;
        if (page.owner != d.owner || page.database_identity != d.database_identity || page.high_water_id != d.high_water_id || page.after_id != actual->cursor || page.next_id <= page.after_id || page.next_id > d.high_water_id || page.rows.size() != observations.size() || page.rows.size() > static_cast<std::size_t>(d.page_size)) throw std::invalid_argument("invalid discovery page");
        auto counts = actual->counts;
        auto previous = page.after_id;
        for (std::size_t i = 0; i < page.rows.size(); ++i) {
            const auto &source = page.rows[i];
            const auto &value = observations[i];
            if (source.identity.owner != d.owner || source.identity.resource_id <= previous || source.identity.resource_id > page.next_id || value.resource != source.identity || value.generation != source.generation || value.source_identity != source.source_identity || value.values_digest != source.values_digest || value.candidate_path != model::resourceDiscoveryCandidate(source, d) || !value.observed_at || value.observed_at > jobs::now() + 300) throw std::invalid_argument("discovery observation source changed");
            previous = source.identity.resource_id;
            const auto record = discovery::observationJson(value);
            (void)co_await jobs::query(connection, "resource_discovery_observations/insert", d.run_id, value.resource.resource_id, value.generation, value.source_identity, value.values_digest, value.grade, record.dump(), value.observed_at);
            ++counts.at(value.grade);
        }
        const auto changed = co_await jobs::query(connection, "resource_discovery_runs/commit", page.next_id, static_cast<std::uint64_t>(observations.size()), nlohmann::json(counts).dump(), page.next_id == d.high_water_id ? std::string("completed") : std::string("running"), d.run_id, actual->claim_generation, actual->cursor);
        if (changed.affected_rows() != 1) throw std::logic_error("discovery lease expired before checkpoint");
        co_return true;
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work), true), true);
}
application::result::ServiceResult<model::ResourceDiscoveryRun> MySqlResourceDiscovery::control(std::string id, std::string action)
{
    auto work = [id = std::move(id), action = std::move(action)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceDiscoveryRun> {
        if (action != "pause" && action != "resume" && action != "cancel") throw std::invalid_argument("invalid discovery action");
        (void)co_await jobs::targets(connection);
        auto actual = co_await discovery::read(connection, id, true);
        if (!actual) throw jobs::MissingOperation();
        if (actual->state == "completed" || actual->state == "cancelled") {
            if (actual->state == "cancelled" && action == "cancel") co_return *actual;
            throw std::logic_error("terminal discovery run");
        }
        const std::string desired = action == "pause" ? "paused" : action == "resume" ? "running" :
                                                                                        "cancelled";
        if (actual->state == desired) co_return *actual;
        if (desired == "running") {
            co_await discovery::target(connection, actual->definition, false);
            const auto active = co_await jobs::query(connection, "resource_discovery_runs/active", std::string(model::resourceDestinationName(actual->definition.destination)));
            if (!active.rows().empty()) throw std::logic_error("another discovery run is active");
        }
        (void)co_await jobs::query(connection, "resource_discovery_runs/control", desired, action == "resume" ? std::string {} : action, id);
        co_return *(co_await discovery::read(connection, id, false));
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceDiscoveryRun>(pool_, std::move(work), true), true);
}
application::result::ServiceResult<bool> MySqlResourceDiscovery::release(const model::ResourceDiscoveryRun &lease, std::string code)
{
    auto work = [lease, code = std::move(code)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (!jobs::code(code)) throw std::invalid_argument("invalid discovery failure code");
        (void)co_await jobs::targets(connection);
        const auto actual = co_await discovery::read(connection, lease.definition.run_id, true);
        if (!actual || actual->state != "running" || actual->definition_hash != lease.definition_hash || actual->claim_generation != lease.claim_generation || actual->cursor != lease.cursor) co_return false;
        const auto changed = co_await jobs::query(connection, "resource_discovery_runs/release", code, lease.definition.run_id, lease.claim_generation);
        co_return changed.affected_rows() == 1;
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work), true), true);
}
application::result::ServiceResult<std::vector<model::ResourceDiscoveryObservation>> MySqlResourceDiscovery::observations(std::string id, std::uint64_t after, std::uint64_t upper, std::size_t limit)
{
    auto work = [id = std::move(id), after, upper, limit](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::vector<model::ResourceDiscoveryObservation>> {
        if (!limit || limit > 500 || after > upper) throw std::invalid_argument("invalid observation page");
        const auto run = co_await discovery::read(connection, id, false);
        if (!run) throw jobs::MissingOperation();
        if (upper > run->cursor) throw std::invalid_argument("observation export exceeds committed cursor");
        const auto rows = co_await jobs::query(connection, "resource_discovery_observations/page", id, after, upper, static_cast<std::uint64_t>(limit));
        std::vector<model::ResourceDiscoveryObservation> result;
        for (const auto row : rows.rows()) result.push_back(discovery::observationFromJson(nlohmann::json::parse(MySqlValueReader::stringAt(row, 0))));
        co_return result;
    };
    return jobs::run(pool_, jobs::transaction<std::vector<model::ResourceDiscoveryObservation>>(pool_, std::move(work)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
