#include "MySqlResourceBaselineSupport.h"
#include <openssl/evp.h>
#include <algorithm>
#include <set>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline {
namespace jobs = transfer_jobs;

std::string digest(std::string_view value)
{
    std::array<unsigned char, 32> bytes {};
    unsigned int size = 0;
    if (EVP_Digest(value.data(), value.size(), bytes.data(), &size, EVP_sha256(), nullptr) != 1 || size != bytes.size()) throw std::runtime_error("baseline digest failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    for (const auto byte : bytes) {
        output.push_back(hex[byte >> 4]);
        output.push_back(hex[byte & 15]);
    }
    return output;
}

boost::asio::awaitable<Review> inspect(boost::mysql::pooled_connection &connection, const model::ResourceBaselinePlan &plan, bool block_excluded)
{
    if (plan.baseline_id.empty() || plan.baseline_id.size() > 96 || !std::all_of(plan.baseline_id.begin(), plan.baseline_id.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; })) throw std::invalid_argument("invalid baseline id");
    if (block_excluded && !plan.writers_stopped) throw std::invalid_argument("writer stop confirmation required");
    Review value;
    const auto identity = co_await legacy_import::databaseIdentity(connection);
    value.targets = co_await legacy_import::targets(connection, true);
    value.snapshot = {{"baseline_id", plan.baseline_id}, {"database_identity", identity}, {"scopes", nlohmann::json::array()}, {"targets", nlohmann::json::array()}};
    for (const auto &target : value.targets) {
        if (target.dispatch_state != "maintenance" || !jobs::hash(target.fingerprint) || !target.revision) throw std::logic_error("baseline target is not maintained");
        value.snapshot["targets"].push_back({{"destination", model::resourceDestinationName(target.destination)}, {"revision", target.revision}, {"fingerprint", target.fingerprint}});
    }
    const auto idle = co_await jobs::query(connection, "resource_cloud_baselines/idle");
    if (MySqlValueReader::uint64At(idle.rows().front(), 0)) throw std::logic_error("baseline has unresolved work");
    std::set<std::string> unique;
    for (std::size_t index = 0; index < plan.import_runs.size(); ++index) {
        const auto owner = static_cast<model::ResourceOwner>(index);
        const auto name = std::string(model::resourceOwnerName(owner));
        const auto &run = plan.import_runs[index];
        if (run.empty() || run.size() > 96 || !unique.insert(run).second) throw std::invalid_argument("four distinct import runs required");
        std::uint64_t high = 0, processed = 0, imported = 0;
        std::array<std::uint64_t, 2> control_generations {};
        std::string manifest;
        for (std::size_t target_index = 1; target_index < 3; ++target_index) {
            const auto rows = co_await jobs::query(connection, "resource_cloud_baselines/runs", run + ':' + std::string(model::resourceDestinationName(value.targets[target_index].destination)));
            if (rows.rows().size() != 1) throw std::logic_error("missing baseline import run");
            const auto row = rows.rows().front();
            const bool supplemented = !plan.cloud_supplement_runs[index][target_index - 1].empty();
            // 空范围没有旧目标对象；随后仍要求当前MAX(id)=0，非空范围必须显式补充。
            const bool empty_scope = MySqlValueReader::uint64At(row, 5) == 0 && MySqlValueReader::uint64At(row, 8) == 0 && MySqlValueReader::uint64At(row, 9) == 0;
            if (!jobs::hash(MySqlValueReader::stringAt(row, 0)) || MySqlValueReader::stringAt(row, 1) != identity || MySqlValueReader::stringAt(row, 2) != name || MySqlValueReader::stringAt(row, 3) != model::resourceDestinationName(value.targets[target_index].destination) || (!supplemented && !empty_scope && MySqlValueReader::uint64At(row, 4) != value.targets[target_index].revision) || MySqlValueReader::stringAt(row, 7) != "completed" || MySqlValueReader::uint64At(row, 5) != MySqlValueReader::uint64At(row, 6) || MySqlValueReader::uint64At(row, 10) || MySqlValueReader::stringAt(row, 11) != "import_attested_skip" || MySqlValueReader::stringAt(row, 12) != "active") throw std::logic_error("baseline run is incomplete or conflicting");
            control_generations[target_index - 1] = MySqlValueReader::uint64At(row, 13);
            if (target_index == 1) {
                manifest = MySqlValueReader::stringAt(row, 0);
                high = MySqlValueReader::uint64At(row, 5);
                processed = MySqlValueReader::uint64At(row, 8);
                imported = MySqlValueReader::uint64At(row, 9);
            } else if (manifest != MySqlValueReader::stringAt(row, 0) || high != MySqlValueReader::uint64At(row, 5) || processed != MySqlValueReader::uint64At(row, 8) || imported != MySqlValueReader::uint64At(row, 9))
                throw std::logic_error("baseline provider runs diverged");
        }
        const auto maximum = co_await jobs::query(connection, jobs::table(owner) + "/cloud_import_max_id");
        if (MySqlValueReader::uint64At(maximum.rows().front(), 0) != high) throw std::logic_error("baseline high water changed");
        auto supplements = co_await readSupplements(connection, plan, index, value, high, processed);
        std::uint64_t cursor = 0, seen = 0, eligible = 0;
        while (cursor < high) {
            const auto current = co_await jobs::query(connection, jobs::table(owner) + "/cloud_import_snapshot", cursor, high, std::uint64_t(500));
            if (current.rows().empty()) throw std::logic_error("baseline rows disappeared");
            const auto end = MySqlValueReader::uint64At(current.rows().back(), 0);
            const auto audit = co_await jobs::query(connection, "resource_cloud_baselines/audit", run, name, cursor, end);
            if (audit.rows().size() != current.rows().size()) throw std::logic_error("baseline audit coverage differs");
            std::vector<model::LegacyResourceSnapshot> sources;
            for (std::size_t i = 0; i < current.rows().size(); ++i) {
                const auto resource = legacy_import::snapshot(owner, current.rows()[i]);
                sources.push_back(resource);
                const auto evidence = audit.rows()[i];
                const auto disposition = MySqlValueReader::stringAt(evidence, 4);
                if (resource.identity.resource_id != MySqlValueReader::uint64At(evidence, 0) || resource.generation != MySqlValueReader::uint64At(evidence, 1) || resource.source_identity != MySqlValueReader::stringAt(evidence, 2) || resource.values_digest != MySqlValueReader::stringAt(evidence, 3) || (disposition != "imported" && disposition != "excluded")) throw std::logic_error("baseline resource changed");
                ++seen;
                if (disposition == "imported")
                    ++eligible;
                else if (block_excluded)
                    (void)co_await jobs::query(connection, "resource_baseline_blocks/insert", name, resource.identity.resource_id, resource.generation, plan.baseline_id);
            }
            const auto policies = co_await jobs::query(connection, jobs::table(owner) + "/baseline_policy_conflicts", run, cursor, end, !supplements[0].run_id.empty(), !supplements[1].run_id.empty());
            if (MySqlValueReader::uint64At(policies.rows().front(), 0)) throw std::logic_error("baseline skip policy changed");
            co_await checkSupplementPage(connection, owner, sources, cursor, end, supplements);
            cursor = end;
        }
        if (seen != processed || eligible != imported) throw std::logic_error("baseline counters differ from audit");
        value.result.resources += seen;
        value.result.blocked_resources += seen - eligible;
        value.snapshot["scopes"].push_back({{"owner", name}, {"run_id", run}, {"manifest", manifest}, {"high_water_id", high}, {"resources", seen}, {"blocked", seen - eligible}});
        auto &scope = value.snapshot["scopes"].back();
        // 默认代次0/无补充保持旧封存JSON及摘要不变。
        if (control_generations[0] || control_generations[1]) scope["control_generations"] = control_generations;
        for (std::size_t i = 0; i < supplements.size(); ++i) {
            const auto &supplement = supplements[i];
            if (supplement.run_id.empty()) continue;
            if (supplement.imported + supplement.allowed_transfers != supplement.expected_imported) throw std::logic_error("supplement accepted count mismatch");
            scope["supplements"][std::string(model::resourceDestinationName(value.targets[i + 1].destination))] = {{"run_id", supplement.run_id}, {"manifest", supplement.manifest}, {"control_generation", supplement.control_generation}, {"imported", supplement.imported}, {"allow_transfer", supplement.allowed_transfers}, {"accepted", supplement.expected_imported}};
        }
    }
    value.result.digest = digest(value.snapshot.dump());
    value.result.state = "reviewed";
    co_return value;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline
