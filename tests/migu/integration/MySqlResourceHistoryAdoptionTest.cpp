#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceHistoryAdoption.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"
#include <chrono>
#include <iostream>
#include <vector>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
namespace {
// 构建真实持久发现观察，所有远端证据均为显式fixture，无网络调用。
std::vector<model::ResourceDiscoveryObservation> observations(mysql::MySqlLegacyResourceImport &sources, mysql::MySqlResourceDiscovery &runs, model::ResourceDiscoveryDefinition d, bool first_missing = false)
{
    test::get(runs.start(d));
    std::vector<model::ResourceDiscoveryObservation> values;
    for (;;) {
        const auto claimed = test::get(runs.claim(d.run_id));
        if (!claimed) break;
        const auto page = test::get(sources.preview(d.owner, claimed->cursor, d.high_water_id, 2));
        std::vector<model::ResourceDiscoveryObservation> batch;
        for (const auto &source : page.rows) {
            const auto index = values.size() + batch.size();
            model::ResourceObjectObservation object;
            object.state = index == 1 || index == 4 || (first_missing && index == 0) ? "missing" : index == 2 ? "unknown" :
                                                                                                                "found";
            if (object.state == "found") {
                object.size_bytes = 3;
                if (index == 3) object.hashes["md5"] = std::string(32, 'a');
            }
            auto value = model::classifyResourceDiscovery(source, model::resourceDiscoveryCandidate(source, d), object);
            value.observed_at = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            batch.push_back(value);
        }
        test::require(test::get(runs.commit(*claimed, page, batch)), "history discovery fixture commit failed");
        values.insert(values.end(), batch.begin(), batch.end());
    }
    return values;
}
} // namespace
int main()
{
    using namespace test;
    try {
        require(c::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_history_adoption_"), "refusing non-isolated history adoption database");
        auto pool = c::tests::migu::support::makeCoverTestPool();
        activate(pool);
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance'");
        for (int i = 1; i <= 5; ++i) {
            const auto id = singer(pool, i);
            execute(pool, "UPDATE singer_resources SET status=0,size_bytes=3,checksum=CONCAT('md5:',REPEAT('a',32)) WHERE id=" + std::to_string(id));
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, id}, {{"cloud_path", "/Archive/file" + std::to_string(i)}});
        }
        mysql::MySqlLegacyResourceImport sources(pool);
        mysql::MySqlResourceDiscovery runs(pool);
        const auto first = get(sources.preview(model::ResourceOwner::Singer, 0, 0, 2));
        model::ResourceDiscoveryDefinition d;
        d.run_id = "history-discovery";
        d.owner = model::ResourceOwner::Singer;
        d.database_identity = first.database_identity;
        d.target_revision = first.targets[1].revision;
        d.target_fingerprint = first.targets[1].fingerprint;
        d.high_water_id = first.high_water_id;
        d.page_size = 2;
        d.strip_prefix = "/Archive";
        const auto values = observations(sources, runs, d);
        require(values.size() == 5, "history discovery fixture lost rows");
        model::ResourceHistoryAdoptionPlan plan;
        plan.run_id = "history-reviewed";
        plan.manifest_hash = std::string(64, 'f');
        plan.report_hash = std::string(64, 'e');
        plan.discovery_run_id = d.run_id;
        plan.discovery_definition_hash = get(runs.find(d.run_id)).definition_hash;
        plan.database_identity = d.database_identity;
        plan.owner = d.owner;
        plan.destination = d.destination;
        plan.target_revision = d.target_revision;
        plan.target_fingerprint = d.target_fingerprint;
        plan.high_water_id = d.high_water_id;
        plan.report_count = values.size();
        mysql::MySqlResourceHistoryAdoption adoption(pool);
        model::ResourceHistoryAdoptionPage page1 {0, values[1].resource.resource_id, {{values[0], "adopt"}, {values[1], "allow_transfer"}}};
        require(!adoption.apply(plan, page1).isSuccess() && get(adoption.progress(plan)).state == "not_started", "history accepted without explicit attestation");
        plan.operator_attested_destination = true;
        plan.writers_stopped = true;
        auto progress = get(adoption.apply(plan, page1));
        require(progress.processed == 2 && progress.imported == 2 && progress.conflicts == 0, "single destination decisions failed for status0 resources");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='baidu' AND transfer_policy='skip' AND verification='operator_attested' AND confirmed_content_generation=0 AND uploaded_at IS NULL AND verified_at IS NULL") == 1, "metadata evidence became strong upload verification");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='baidu' AND transfer_policy='normal' AND skip_reason='' AND baseline_id='history-reviewed' AND policy_content_generation=1 AND policy_target_revision=1") == 1, "missing decision did not allow only selected destination");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive'") == 0 && scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE local_skip_generation<>0") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == 0, "history adoption altered another lane or enqueued content");
        require(get(adoption.apply(plan, page1)).processed == 2 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='history-reviewed'") == 2, "history replay duplicated audit or progress");
        auto other_definition = d;
        other_definition.run_id = "history-discovery-gdrive";
        other_definition.destination = model::ResourceDestination::Gdrive;
        other_definition.target_revision = first.targets[2].revision;
        other_definition.target_fingerprint = first.targets[2].fingerprint;
        const auto other_values = observations(sources, runs, other_definition, true);
        auto other_destination = plan;
        other_destination.manifest_hash = std::string(64, 'c');
        other_destination.destination = other_definition.destination;
        other_destination.target_revision = other_definition.target_revision;
        other_destination.target_fingerprint = other_definition.target_fingerprint;
        other_destination.discovery_run_id = other_definition.run_id;
        other_destination.discovery_definition_hash = get(runs.find(other_definition.run_id)).definition_hash;
        const model::ResourceHistoryAdoptionPage opposite_page {0, other_values[1].resource.resource_id, {{other_values[0], "allow_transfer"}, {other_values[1], "allow_transfer"}}};
        require(!adoption.apply(other_destination, opposite_page).isSuccess() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id='history-reviewed:gdrive'") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive'") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='history-reviewed'") == 2, "opposite destination reused run audit or left partial decisions");
        auto forged = plan;
        forged.run_id = "history-forged";
        auto invalid = page1;
        invalid.rows[0].observation.candidate_path = "other/object";
        require(!adoption.apply(forged, invalid).isSuccess() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id='history-forged:baidu'") == 0, "forged observation left a partial run");
        invalid = page1;
        invalid.rows.pop_back();
        require(!adoption.apply(forged, invalid).isSuccess(), "history manifest omitted persisted observations");
        progress = get(adoption.control(plan, "pause"));
        require(progress.control_state == "paused" && progress.control_generation == 1, "history pause did not fence pages");
        model::ResourceHistoryAdoptionPage page2 {page1.next_id, values[3].resource.resource_id, {{values[2], "review"}, {values[3], "adopt"}}};
        require(!adoption.apply(plan, page2).isSuccess(), "paused history accepted old page");
        plan.control_generation = progress.control_generation;
        progress = get(adoption.control(plan, "resume"));
        require(progress.control_generation == 2, "history resume did not advance generation");
        require(!adoption.apply(plan, page2).isSuccess(), "page from before pause/resume was adopted");
        plan.control_generation = progress.control_generation;
        execute(pool, "UPDATE singer_resources SET size_bytes=4 WHERE id=" + std::to_string(values[3].resource.resource_id));
        execute(pool, "CREATE TRIGGER history_policy_refuse BEFORE INSERT ON resource_cloud_migration_audit FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture page rejection'");
        require(!adoption.apply(plan, page2).isSuccess() && get(adoption.progress(plan)).cursor == page1.next_id && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='history-reviewed'") == 2, "history page fault partially advanced audit/cursor");
        execute(pool, "DROP TRIGGER history_policy_refuse");
        progress = get(adoption.apply(plan, page2));
        require(progress.processed == 4 && progress.conflicts == 2 && progress.imported == 2, "unknown or source change was silently adopted");
        execute(pool, "CREATE TRIGGER history_control_refuse BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture control rejection'");
        require(!adoption.control(plan, "cancel").isSuccess() && get(adoption.progress(plan)).control_state == "active", "control audit failure left cancelled state");
        execute(pool, "DROP TRIGGER history_control_refuse");
        const auto id5 = std::to_string(values[4].resource.resource_id);
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,singer_resource_id,content_generation,target_revision,confirmed_content_generation,confirmed_target_revision,verification,checksum,size_bytes,cloud_path) VALUES('singer'," + id5 + ",'baidu'," + id5 + ",1,1,1,1,'sha256_verified',REPEAT('a',64),3,'verified/object')");
        const model::ResourceHistoryAdoptionPage page3 {page2.next_id, plan.high_water_id, {{values[4], "allow_transfer"}}};
        progress = get(adoption.apply(plan, page3));
        require(progress.state == "completed" && progress.processed == 5 && progress.imported == 2 && progress.conflicts == 3, "same-version strong receipt contradiction did not remain a conflict");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE singer_resource_id=" + id5 + " AND confirmed_content_generation=1 AND verification='sha256_verified'") == 1 && !adoption.control(plan, "resume").isSuccess(), "history dropped confirmed facts or revived completed run");
        auto cancellable = plan;
        cancellable.run_id = "history-cancel";
        cancellable.control_generation = 0;
        get(adoption.apply(cancellable, page1));
        auto cancelled = get(adoption.control(cancellable, "cancel"));
        cancellable.control_generation = cancelled.control_generation;
        require(!adoption.control(cancellable, "resume").isSuccess() && !adoption.apply(cancellable, page2).isSuccess(), "cancelled history run resumed");
        auto stale = plan;
        stale.run_id = "history-target";
        stale.control_generation = 0;
        execute(pool, "UPDATE resource_destination_states SET target_revision=2 WHERE destination='baidu'");
        require(!adoption.apply(stale, page1).isSuccess() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id='history-target:baidu'") == 0, "stale target accepted history policy");
        std::cout << "PASS: reviewed single destination decisions, source/target guards, immutable observations, pause/resume/cancel, atomic rollback and no media IO\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
