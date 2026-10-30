#include "MySqlResourceBaselineSupport.h"
#include <stdexcept>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline {
namespace jobs = transfer_jobs;
boost::asio::awaitable<std::array<CloudSupplement, 2>> readSupplements(boost::mysql::pooled_connection &connection, const model::ResourceBaselinePlan &plan, std::size_t owner_index, const Review &review, std::uint64_t high, std::uint64_t processed)
{
    std::array<CloudSupplement, 2> supplements;
    const auto owner = std::string(model::resourceOwnerName(static_cast<model::ResourceOwner>(owner_index)));
    const auto identity = review.snapshot.at("database_identity").get<std::string>();
    for (std::size_t i = 0; i < 2; ++i) {
        auto &supplement = supplements[i];
        supplement.run_id = plan.cloud_supplement_runs[owner_index][i];
        if (supplement.run_id.empty()) continue;
        if (supplement.run_id.size() > 96 || supplement.run_id == plan.import_runs[owner_index]) throw std::invalid_argument("invalid baseline supplement id");
        const auto &target = review.targets[i + 1];
        const auto name = std::string(model::resourceDestinationName(target.destination));
        const auto rows = co_await jobs::query(connection, "resource_cloud_baselines/runs", supplement.run_id + ':' + name);
        if (rows.rows().size() != 1) throw std::logic_error("baseline supplement missing");
        const auto row = rows.rows().front();
        supplement.manifest = MySqlValueReader::stringAt(row, 0);
        supplement.expected_imported = MySqlValueReader::uint64At(row, 9);
        supplement.control_generation = MySqlValueReader::uint64At(row, 13);
        if (!jobs::hash(supplement.manifest) || MySqlValueReader::stringAt(row, 1) != identity || MySqlValueReader::stringAt(row, 2) != owner || MySqlValueReader::stringAt(row, 3) != name || MySqlValueReader::uint64At(row, 4) != target.revision || MySqlValueReader::uint64At(row, 5) != high || MySqlValueReader::uint64At(row, 6) != high || MySqlValueReader::stringAt(row, 7) != "completed" || MySqlValueReader::uint64At(row, 8) != processed || MySqlValueReader::uint64At(row, 10) || MySqlValueReader::stringAt(row, 11) != "import_discovery_skip" || MySqlValueReader::stringAt(row, 12) != "active") throw std::logic_error("baseline supplement incomplete or stale");
    }
    co_return supplements;
}
boost::asio::awaitable<void> checkSupplementPage(boost::mysql::pooled_connection &connection, model::ResourceOwner owner, const std::vector<model::LegacyResourceSnapshot> &sources, std::uint64_t after, std::uint64_t end, std::array<CloudSupplement, 2> &supplements)
{
    const auto name = std::string(model::resourceOwnerName(owner));
    for (std::size_t i = 0; i < 2; ++i) {
        auto &supplement = supplements[i];
        if (supplement.run_id.empty()) continue;
        const auto audit = co_await jobs::query(connection, "resource_cloud_baselines/audit", supplement.run_id, name, after, end);
        if (audit.rows().size() != sources.size()) throw std::logic_error("supplement audit coverage differs");
        for (std::size_t j = 0; j < sources.size(); ++j) {
            const auto &source = sources[j];
            const auto row = audit.rows()[j];
            const auto disposition = MySqlValueReader::stringAt(row, 4);
            if (source.identity.resource_id != MySqlValueReader::uint64At(row, 0) || source.generation != MySqlValueReader::uint64At(row, 1) || source.source_identity != MySqlValueReader::stringAt(row, 2) || source.values_digest != MySqlValueReader::stringAt(row, 3) || (disposition != "imported" && disposition != "allow_transfer")) throw std::logic_error("supplement source changed or not reviewed");
            if (disposition == "imported")
                ++supplement.imported;
            else
                ++supplement.allowed_transfers;
        }
        const auto destination = std::string(model::resourceDestinationName(static_cast<model::ResourceDestination>(i + 1)));
        const auto policies = co_await jobs::query(connection, "resource_cloud_baselines/history_policy_conflicts", destination, supplement.run_id, name, after, end);
        if (MySqlValueReader::uint64At(policies.rows().front(), 0)) throw std::logic_error("supplement policy changed");
    }
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline
