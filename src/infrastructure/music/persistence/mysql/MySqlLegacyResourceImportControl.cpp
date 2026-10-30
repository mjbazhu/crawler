#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "MySqlLegacyResourceImportSupport.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace model = application::model;
namespace jobs = transfer_jobs;
application::result::ServiceResult<model::LegacyResourceImportProgress> MySqlLegacyResourceImport::control(model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage scope, std::string action)
{
    auto work = [plan, scope, action = std::move(action)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (action != "pause" && action != "resume" && action != "cancel") throw std::invalid_argument("invalid import control");
        if (plan.run_id.empty() || plan.run_id.size() > 96 || !jobs::hash(plan.manifest_hash) || co_await legacy_import::databaseIdentity(connection) != scope.database_identity) throw std::invalid_argument("invalid import control scope");
        const auto targets = co_await legacy_import::targets(connection, true);
        if (action == "resume")
            for (std::size_t i = 0; i < targets.size(); ++i)
                if (targets[i].dispatch_state != "maintenance" || targets[i].revision != scope.targets[i].revision || targets[i].fingerprint != scope.targets[i].fingerprint) throw std::logic_error("import target changed");
        const std::string desired = action == "pause" ? "paused" : action == "resume" ? "active" :
                                                                                        "cancelled";
        std::string before;
        std::uint64_t cursor = 0;
        bool first = true;
        for (const auto destination : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            const auto name = std::string(model::resourceDestinationName(destination));
            const auto id = plan.run_id + ':' + name;
            const auto rows = co_await jobs::query(connection, "resource_cloud_backfills/lock", id);
            if (rows.rows().size() != 1) throw std::logic_error("import control run missing");
            const auto row = rows.rows().front();
            if (MySqlValueReader::stringAt(row, 0) != plan.manifest_hash || MySqlValueReader::stringAt(row, 1) != scope.database_identity || MySqlValueReader::stringAt(row, 2) != model::resourceOwnerName(scope.owner) || MySqlValueReader::stringAt(row, 3) != name || MySqlValueReader::uint64At(row, 4) != scope.targets[static_cast<std::size_t>(destination)].revision || MySqlValueReader::uint64At(row, 5) != scope.high_water_id || MySqlValueReader::stringAt(row, 10) != "import_attested_skip" || MySqlValueReader::uint64At(row, 9) != plan.control_generation) throw std::logic_error("import control definition changed");
            const auto state = MySqlValueReader::stringAt(row, 8);
            // 新控制列默认active不能复活升级前已取消的进度终态。
            if (MySqlValueReader::stringAt(row, 7) == "cancelled" && action != "cancel") throw std::logic_error("legacy import was cancelled");
            const auto value = MySqlValueReader::uint64At(row, 6);
            if (!first && (state != before || value != cursor)) throw std::logic_error("import controls diverged");
            before = state;
            cursor = value;
            first = false;
            if (MySqlValueReader::stringAt(row, 7) == "completed" || (action == "resume" && state != "paused") || (state == "cancelled" && desired != "cancelled")) throw std::logic_error("terminal import cannot resume");
            if (state == desired) continue;
            const auto changed = co_await jobs::query(connection, "resource_cloud_backfills/control", desired, id, plan.manifest_hash, plan.control_generation);
            if (changed.affected_rows() != 1) throw std::logic_error("import control changed");
        }
        if (before != desired) {
            const auto payload = nlohmann::json {{"manifest_sha256", plan.manifest_hash}, {"control_generation", plan.control_generation + 1}, {"cursor", cursor}}.dump();
            (void)co_await jobs::query(connection, "resource_cloud_backfills/control_audit", plan.run_id, before, desired, payload);
        }
        co_return true;
    };
    const auto controlled = jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work), true), true);
    if (!controlled.isSuccess()) return application::result::ServiceResult<model::LegacyResourceImportProgress>::failure(controlled.error(), controlled.message());
    return progress(std::move(plan), std::move(scope));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
