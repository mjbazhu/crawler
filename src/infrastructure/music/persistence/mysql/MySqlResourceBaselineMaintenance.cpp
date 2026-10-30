#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include "MySqlResourceBaselineSupport.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
application::result::ServiceResult<application::model::ResourceBaselineReview> MySqlResourceBaseline::maintenance(std::string id, std::string expected, bool stopped)
{
    namespace jobs = transfer_jobs;
    namespace model = application::model;
    auto work = [id = std::move(id), expected = std::move(expected), stopped](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceBaselineReview> {
        if (!stopped || id.empty() || id.size() > 96 || !jobs::hash(expected)) throw std::invalid_argument("maintenance requires frozen baseline and stopped writers");
        // 与封存/激活一致：先锁基线，再锁固定顺序的三个目标；不取资源/任务锁。
        const auto stored = co_await jobs::query(connection, "resource_cloud_baselines/find", id);
        if (stored.rows().size() != 1 || MySqlValueReader::stringAt(stored.rows().front(), 0) != expected) throw std::logic_error("maintenance baseline changed");
        const auto frozen = nlohmann::json::parse(MySqlValueReader::stringAt(stored.rows().front(), 1));
        const auto targets = co_await legacy_import::targets(connection, true);
        const auto markers = co_await jobs::query(connection, "resource_destination_states/baseline_current");
        if (markers.rows().size() != 3 || frozen.at("targets").size() != 3) throw std::logic_error("maintenance targets missing");
        bool unchanged = true;
        nlohmann::json before = nlohmann::json::array();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto &target = targets[i];
            if (target.revision != frozen["targets"][i]["revision"].get<std::uint64_t>() || target.fingerprint != frozen["targets"][i]["fingerprint"].get<std::string>()) throw std::logic_error("maintenance target changed");
            unchanged = unchanged && target.dispatch_state == "maintenance";
            before.push_back({{"destination", model::resourceDestinationName(target.destination)}, {"state", target.dispatch_state}, {"revision", target.revision}});
        }
        for (const auto row : markers.rows()) unchanged = unchanged && MySqlValueReader::uint64At(row, 1) == 0 && MySqlValueReader::stringAt(row, 2).empty();
        model::ResourceBaselineReview result {expected, 0, 0, "maintenance"};
        for (const auto &scope : frozen.at("scopes")) {
            result.resources += scope.at("resources").get<std::uint64_t>();
            result.blocked_resources += scope.at("blocked").get<std::uint64_t>();
        }
        if (unchanged) {
            if (MySqlValueReader::stringAt(stored.rows().front(), 2) != "sealed") throw std::logic_error("maintenance baseline state is inconsistent");
            co_return result;
        }
        if (MySqlValueReader::stringAt(stored.rows().front(), 2) != "active") throw std::logic_error("maintenance baseline is not current");
        for (const auto row : markers.rows())
            if (MySqlValueReader::uint64At(row, 1) != 1 || MySqlValueReader::stringAt(row, 2) != expected) throw std::logic_error("maintenance baseline markers changed");
        // 不清理任务/槽/未决意图；重新激活仍须经过inspect的全部占用与清单围栏。
        (void)co_await jobs::query(connection, "resource_destination_states/enter_maintenance");
        (void)co_await jobs::query(connection, "governance_events/baseline_maintenance", before.dump(), id);
        (void)co_await jobs::query(connection, "resource_cloud_baselines/reseal", id, expected);
        co_return result;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceBaselineReview>(pool_, std::move(work)), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
