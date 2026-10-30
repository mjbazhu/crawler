#include "MySqlResourceHistorySupport.h"
#include <map>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
namespace history {
boost::asio::awaitable<bool> adopt(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &p, const model::ResourceHistoryAdoptionRow &row, const model::LegacyResourceSnapshot &source)
{
    const auto &o = row.observation;
    if (source.identity != o.resource || source.generation != o.generation || source.source_identity != o.source_identity || source.values_digest != o.values_digest || source.status == 4 || !jobs::hash(source.source_identity)) co_return false;
    const bool skip = row.action == "adopt" && (o.grade == "observed" || o.grade == "hash_matched");
    const bool allow = row.action == "allow_transfer" && o.grade == "missing";
    if (!skip && !allow) co_return false;
    const auto owner = std::string(model::resourceOwnerName(p.owner)), destination = std::string(model::resourceDestinationName(p.destination));
    const auto id = source.identity.resource_id;
    const auto active = co_await jobs::query(connection, "resource_cloud_backfills/history_active", owner, id, destination);
    if (!active.rows().empty()) co_return false;
    const auto receipt = co_await jobs::query(connection, "resource_cloud_receipts/transfer_lock", owner, id, destination);
    if (!receipt.rows().empty()) {
        const auto value = receipt.rows().front();
        const auto state = MySqlValueReader::stringAt(value, 0);
        if (state == "running" || state == "uncertain" || MySqlValueReader::uint64At(value, 1) > o.generation || MySqlValueReader::uint64At(value, 2) > p.target_revision) co_return false;
        const auto verification = MySqlValueReader::stringAt(value, 13);
        if (allow && MySqlValueReader::uint64At(value, 3) == o.generation && MySqlValueReader::uint64At(value, 4) == p.target_revision && (verification == "sha256_verified" || verification == "hash_matched")) co_return false;
    }
    const auto evidence = nlohmann::json {{"manifest_sha256", p.manifest_hash}, {"report_sha256", p.report_hash}, {"discovery_run_id", p.discovery_run_id}, {"discovery_definition_hash", p.discovery_definition_hash}, {"run_id", p.run_id}, {"action", row.action}, {"observation", discovery::observationJson(o)}}.dump();
    (void)co_await jobs::query(connection, "resource_cloud_receipts/history_policy", owner, id, destination, p.owner == model::ResourceOwner::Song ? id : 0, p.owner == model::ResourceOwner::Singer ? id : 0, p.owner == model::ResourceOwner::Album ? id : 0, p.owner == model::ResourceOwner::SongArtifact ? id : 0, o.generation, p.target_revision, skip ? std::string("skip") : std::string("normal"), o.generation, p.target_revision, p.run_id, skip ? std::string("history_discovery_review") : std::string {}, skip ? std::string("skipped") : std::string("pending"), skip ? std::string("operator_attested") : std::string("unverified"), o.candidate_path, evidence);
    co_return true;
}
} // namespace history
application::result::ServiceResult<model::ResourceHistoryAdoptionProgress> MySqlResourceHistoryAdoption::apply(model::ResourceHistoryAdoptionPlan plan, model::ResourceHistoryAdoptionPage page)
{
    auto work = [plan = std::move(plan), page = std::move(page)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceHistoryAdoptionProgress> {
        history::validate(plan);
        if (!plan.operator_attested_destination || !plan.writers_stopped || page.rows.size() > 200 || page.after_id > page.next_id || page.next_id > plan.high_water_id || (plan.high_water_id && page.after_id == page.next_id)) throw std::invalid_argument("history adoption requires reviewed page and stopped writers");
        auto previous = page.after_id;
        for (const auto &row : page.rows) {
            if (row.observation.resource.owner != plan.owner || row.observation.resource.resource_id <= previous || row.observation.resource.resource_id > page.next_id || (row.action != "adopt" && row.action != "allow_transfer" && row.action != "review")) throw std::invalid_argument("invalid history adoption page");
            (void)discovery::observationJson(row.observation);
            previous = row.observation.resource.resource_id;
        }
        // 与旧声明导入保持资源→目标→run的锁顺序；不可变档案的JSON_TABLE不参与锁定。
        auto text = legacy_import::load(legacy_import::table(plan.owner) + "/cloud_import_snapshot") + " FOR UPDATE OF r,s";
        auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
        boost::mysql::results resource_rows;
        co_await connection->async_execute(statement.bind(page.after_id, page.next_id, std::uint64_t(201)), resource_rows, boost::asio::use_awaitable);
        std::map<std::uint64_t, model::LegacyResourceSnapshot> sources;
        for (const auto row : resource_rows.rows()) {
            auto source = legacy_import::snapshot(plan.owner, row);
            sources.emplace(source.identity.resource_id, std::move(source));
        }
        co_await history::scope(connection, plan, true);
        const auto owner = std::string(model::resourceOwnerName(plan.owner)), destination = std::string(model::resourceDestinationName(plan.destination));
        // 裸run_id也是审计唯一键的一部分，不能让另一平台共享同一审计空间。
        const auto occupied = co_await jobs::query(connection, "resource_cloud_backfills/history_scope", plan.run_id + ":baidu", plan.run_id + ":gdrive");
        for (const auto row : occupied.rows())
            if (MySqlValueReader::stringAt(row, 0) != destination || MySqlValueReader::stringAt(row, 1) != "import_discovery_skip") throw std::logic_error("history adoption run belongs to another scope");
        (void)co_await jobs::query(connection, "resource_cloud_backfills/history_start", history::key(plan), plan.manifest_hash, plan.database_identity, owner, destination, plan.target_revision, plan.high_water_id);
        const auto before = co_await history::read(connection, plan, true);
        if (before.control_state != "active" || before.control_generation != plan.control_generation) throw std::logic_error("history adoption control changed");
        if (before.cursor >= page.next_id && (page.next_id > page.after_id || before.state == "completed")) co_return before;
        if (before.cursor != page.after_id) throw std::logic_error("history adoption cursor changed");
        const auto observations = co_await jobs::query(connection, "resource_discovery_observations/page", plan.discovery_run_id, page.after_id, page.next_id, std::uint64_t(201));
        if (observations.rows().size() != page.rows.size()) throw std::logic_error("history manifest omitted observations");
        for (std::size_t i = 0; i < page.rows.size(); ++i) {
            if (nlohmann::json::parse(MySqlValueReader::stringAt(observations.rows()[i], 0)) != discovery::observationJson(page.rows[i].observation)) throw std::logic_error("history observation changed");
        }
        std::uint64_t imported = 0, conflicts = 0;
        for (const auto &row : page.rows) {
            const auto &o = row.observation;
            const auto found = sources.find(o.resource.resource_id);
            const bool accepted = found != sources.end() && co_await history::adopt(connection, plan, row, found->second);
            const auto disposition = accepted ? (row.action == "allow_transfer" ? "allow_transfer" : "imported") : "conflict";
            if (accepted)
                ++imported;
            else
                ++conflicts;
            const auto values = found == sources.end() ? nlohmann::json {{"source_missing", true}}.dump() : found->second.legacy_values;
            (void)co_await jobs::query(connection, "resource_cloud_migration_audit/import_row", plan.run_id, owner, o.resource.resource_id, o.source_identity, o.generation, values, std::string(disposition));
        }
        const auto processed = before.processed + page.rows.size();
        if (processed > plan.report_count || (page.next_id == plan.high_water_id && processed != plan.report_count)) throw std::logic_error("history report count changed");
        const auto updated = co_await jobs::query(connection, "resource_cloud_backfills/finish_page", page.next_id, static_cast<std::uint64_t>(page.rows.size()), imported, conflicts, page.next_id == plan.high_water_id ? std::string("completed") : std::string("paused"), history::key(plan), plan.manifest_hash, page.after_id);
        if (updated.affected_rows() != 1) throw std::logic_error("history checkpoint changed");
        co_return co_await history::read(connection, plan, false);
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceHistoryAdoptionProgress>(pool_, std::move(work), true), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
