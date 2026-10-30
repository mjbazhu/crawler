#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include "MySqlResourceBaselineSupport.h"
#include <algorithm>
#include <set>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline {
namespace jobs = transfer_jobs;

boost::asio::awaitable<model::ResourceBaselineReview> seal(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceBaselinePlan plan, std::string expected)
{
    if (!jobs::hash(expected) || !plan.writers_stopped) throw std::invalid_argument("review digest and writer stop confirmation required");
    auto work = [plan, expected](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceBaselineReview> {
        const auto existing = co_await jobs::query(connection, "resource_cloud_baselines/find", plan.baseline_id);
        auto checked = co_await inspect(connection, plan, true);
        if (checked.result.digest != expected) throw std::logic_error("baseline review changed");
        if (existing.rows().empty())
            (void)co_await jobs::query(connection, "resource_cloud_baselines/insert", plan.baseline_id, expected, checked.snapshot.dump());
        else if (MySqlValueReader::stringAt(existing.rows().front(), 0) != expected || MySqlValueReader::stringAt(existing.rows().front(), 1) != checked.snapshot.dump()) {
            // JSON 序列化顺序由 MySQL 规范化，因此比较结构而非文本。
            if (MySqlValueReader::stringAt(existing.rows().front(), 0) != expected || nlohmann::json::parse(MySqlValueReader::stringAt(existing.rows().front(), 1)) != checked.snapshot) throw std::logic_error("baseline identity already used");
        }
        checked.result.state = "sealed";
        co_return checked.result;
    };
    co_return co_await jobs::transaction<model::ResourceBaselineReview>(std::move(pool), std::move(work));
}

boost::asio::awaitable<model::ResourceBaselineReview> activate(std::shared_ptr<MySqlConnectionPool> pool, std::string id, std::string expected, std::vector<model::ResourceDestination> enabled, bool stopped)
{
    if (!stopped || !jobs::hash(expected) || enabled.empty() || enabled.size() > 3) throw std::invalid_argument("activation requires reviewed scope and writer stop confirmation");
    std::set<model::ResourceDestination> selected;
    for (const auto destination : enabled)
        if (model::resourceDestinationName(destination).empty() || !selected.insert(destination).second) throw std::invalid_argument("invalid enabled destinations");
    auto work = [id, expected, selected](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceBaselineReview> {
        const auto stored = co_await jobs::query(connection, "resource_cloud_baselines/find", id);
        if (stored.rows().size() != 1 || MySqlValueReader::stringAt(stored.rows().front(), 0) != expected) throw std::logic_error("sealed baseline missing");
        const auto frozen = nlohmann::json::parse(MySqlValueReader::stringAt(stored.rows().front(), 1));
        if (MySqlValueReader::stringAt(stored.rows().front(), 2) == "active") {
            const auto targets = co_await legacy_import::targets(connection, true);
            for (std::size_t i = 0; i < targets.size(); ++i)
                if (targets[i].revision != frozen["targets"][i]["revision"].get<std::uint64_t>() || targets[i].fingerprint != frozen["targets"][i]["fingerprint"].get<std::string>() || targets[i].dispatch_state != (selected.contains(targets[i].destination) ? "active" : "paused")) throw std::logic_error("active baseline target changed");
            const auto markers = co_await jobs::query(connection, "resource_destination_states/baseline_current");
            if (markers.rows().size() != 3) throw std::logic_error("active baseline markers missing");
            for (const auto row : markers.rows())
                if (MySqlValueReader::uint64At(row, 1) != 1 || MySqlValueReader::stringAt(row, 2) != expected) throw std::logic_error("active baseline markers changed");
            model::ResourceBaselineReview result {expected, 0, 0, "active"};
            for (const auto &scope : frozen.at("scopes")) {
                result.resources += scope.at("resources").get<std::uint64_t>();
                result.blocked_resources += scope.at("blocked").get<std::uint64_t>();
            }
            co_return result;
        }
        model::ResourceBaselinePlan plan;
        plan.baseline_id = id;
        plan.writers_stopped = true;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto &scope = frozen.at("scopes").at(i);
            plan.import_runs[i] = scope.at("run_id").get<std::string>();
            if (scope.contains("supplements")) {
                for (std::size_t target = 0; target < 2; ++target) {
                    const auto name = std::string(model::resourceDestinationName(static_cast<model::ResourceDestination>(target + 1)));
                    if (scope.at("supplements").contains(name)) plan.cloud_supplement_runs[i][target] = scope.at("supplements").at(name).at("run_id").get<std::string>();
                }
            }
        }
        auto checked = co_await inspect(connection, plan, true);
        if (checked.result.digest != expected || checked.snapshot != frozen) throw std::logic_error("sealed baseline changed before activation");
        for (const auto &target : checked.targets) {
            const auto updated = co_await jobs::query(connection, "resource_destination_states/activate_baseline", selected.contains(target.destination) ? std::string("active") : std::string("paused"), expected, std::string(model::resourceDestinationName(target.destination)), target.revision, target.fingerprint);
            if (updated.affected_rows() != 1) throw std::logic_error("target activation changed concurrently");
        }
        (void)co_await jobs::query(connection, "resource_cloud_baselines/activate", id, expected);
        checked.result.state = "active";
        co_return checked.result;
    };
    co_return co_await jobs::transaction<model::ResourceBaselineReview>(std::move(pool), std::move(work));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
MySqlResourceBaseline::MySqlResourceBaseline(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("baseline pool required");
}
application::result::ServiceResult<application::model::ResourceBaselineReview> MySqlResourceBaseline::review(application::model::ResourceBaselinePlan plan)
{
    auto work = [plan](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<application::model::ResourceBaselineReview> { co_return (co_await baseline::inspect(connection, plan, false)).result; };
    return transfer_jobs::run(pool_, transfer_jobs::transaction<application::model::ResourceBaselineReview>(pool_, std::move(work)));
}
application::result::ServiceResult<application::model::ResourceBaselineReview> MySqlResourceBaseline::seal(application::model::ResourceBaselinePlan plan, std::string expected)
{
    return transfer_jobs::run(pool_, baseline::seal(pool_, std::move(plan), std::move(expected)), true);
}
application::result::ServiceResult<application::model::ResourceBaselineReview> MySqlResourceBaseline::activate(std::string id, std::string expected, std::vector<application::model::ResourceDestination> destinations, bool stopped)
{
    return transfer_jobs::run(pool_, baseline::activate(pool_, std::move(id), std::move(expected), std::move(destinations), stopped), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
