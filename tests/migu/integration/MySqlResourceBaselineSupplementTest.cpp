#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceHistoryAdoption.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"
#include <chrono>
#include <iostream>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
using D = model::ResourceDestination;
int main()
{
    using namespace test;
    std::string stage = "setup";
    try {
        require(c::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_supplement_"), "refusing non-isolated supplement database");
        auto pool = c::tests::migu::support::makeCoverTestPool();
        mysql::MySqlLegacyResourceImport importer(pool);
        for (auto destination : {D::Local, D::Baidu, D::Gdrive}) get(importer.configureTarget(destination, 1, std::string(64, char('a' + static_cast<int>(destination)))));
        const auto first = singer(pool, 1), second = singer(pool, 2);
        execute(pool, "UPDATE singer_resources SET status=2,local_path='missing/legacy',size_bytes=3,checksum=CONCAT('md5:',REPEAT('a',32))");
        for (const auto id : {first, second}) c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, id}, {{"cloud_backend", "baidupcs"}, {"cloud_path", "/Old/" + std::to_string(id)}});
        model::ResourceBaselinePlan base;
        base.baseline_id = "old-baseline";
        base.writers_stopped = true;
        for (std::size_t i = 0; i < 4; ++i) {
            base.import_runs[i] = "old-import-" + std::to_string(i);
            model::LegacyResourceImportPlan plan {base.import_runs[i], std::string(64, char('a' + i)), i == 1, {}};
            plan.operator_excluded_all = i != 1;
            get(importer.importPage(plan, get(importer.preview(static_cast<model::ResourceOwner>(i), 0, 0, 500))));
        }
        mysql::MySqlResourceBaseline baseline(pool);
        stage = "original-baseline";
        const auto old_review = get(baseline.review(base));
        get(baseline.seal(base, old_review.digest));
        get(baseline.activate(base.baseline_id, old_review.digest, {D::Local, D::Baidu, D::Gdrive}, true));
        get(baseline.maintenance(base.baseline_id, old_review.digest, true));
        stage = "target-switch";
        require(get(importer.configureTarget(D::Baidu, 1, std::string(64, 'd'))) == 2, "target did not advance revision");
        require(!baseline.review(base).isSuccess(), "old target declaration silently reused");
        const auto scope = get(importer.preview(model::ResourceOwner::Singer, 0, 0, 200));
        model::ResourceDiscoveryDefinition definition;
        definition.run_id = "supplement-discovery";
        definition.owner = model::ResourceOwner::Singer;
        definition.database_identity = scope.database_identity;
        definition.target_revision = 2;
        definition.target_fingerprint = std::string(64, 'd');
        definition.high_water_id = scope.high_water_id;
        definition.strip_prefix = "/Old";
        mysql::MySqlResourceDiscovery discovery(pool);
        stage = "discovery";
        const auto started = get(discovery.start(definition));
        const auto lease = *get(discovery.claim(definition.run_id));
        std::vector<model::ResourceDiscoveryObservation> observations;
        for (const auto &source : scope.rows) {
            model::ResourceObjectObservation object;
            object.state = source.identity.resource_id == first ? "found" : "missing";
            if (object.state == "found") object.size_bytes = 3;
            auto observation = model::classifyResourceDiscovery(source, model::resourceDiscoveryCandidate(source, definition), object);
            observation.observed_at = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            observations.push_back(observation);
        }
        require(get(discovery.commit(lease, scope, observations)), "fixture discovery page failed");
        model::ResourceHistoryAdoptionPlan adoption;
        adoption.run_id = "singer-baidu-supplement";
        adoption.manifest_hash = std::string(64, 'e');
        adoption.report_hash = std::string(64, 'f');
        adoption.discovery_run_id = definition.run_id;
        adoption.discovery_definition_hash = started.definition_hash;
        adoption.database_identity = scope.database_identity;
        adoption.owner = definition.owner;
        adoption.destination = D::Baidu;
        adoption.target_revision = 2;
        adoption.target_fingerprint = definition.target_fingerprint;
        adoption.high_water_id = scope.high_water_id;
        adoption.report_count = observations.size();
        adoption.operator_attested_destination = adoption.writers_stopped = true;
        model::ResourceHistoryAdoptionPage page;
        page.next_id = scope.high_water_id;
        page.rows = {{observations[0], "adopt"}, {observations[1], "allow_transfer"}};
        stage = "adoption";
        const auto applied = get(mysql::MySqlResourceHistoryAdoption(pool).apply(adoption, page));
        require(applied.state == "completed" && applied.imported == 2 && applied.conflicts == 0, "single target decisions did not complete");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE local_skip_baseline='old-import-1'") == 2 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND baseline_id='old-import-1'") == 2, "single target adoption changed local or other cloud");
        require(!baseline.review(base).isSuccess(), "supplement accepted without explicit baseline selection");
        auto supplemented = base;
        stage = "supplement-review";
        supplemented.baseline_id = "supplemented-baseline";
        supplemented.cloud_supplement_runs[1][0] = adoption.run_id;
        const auto review = get(baseline.review(supplemented));
        require(review.resources == 2 && review.blocked_resources == 0, "supplement changed global scope");
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='skip' WHERE cloud_provider='baidu' AND resource_id=" + std::to_string(second));
        require(!baseline.seal(supplemented, review.digest).isSuccess(), "missing decision policy drift accepted");
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='normal' WHERE cloud_provider='baidu' AND resource_id=" + std::to_string(second));
        execute(pool, "UPDATE resource_cloud_backfills SET control_state='paused' WHERE run_id='singer-baidu-supplement:baidu'");
        require(!baseline.review(supplemented).isSuccess(), "paused supplement accepted for activation");
        execute(pool, "UPDATE resource_cloud_backfills SET control_state='active' WHERE run_id='singer-baidu-supplement:baidu'");
        get(baseline.seal(supplemented, review.digest));
        stage = "supplement-activate";
        execute(pool, "UPDATE resource_cloud_backfills SET control_generation=1 WHERE run_id='singer-baidu-supplement:baidu'");
        require(!baseline.activate(supplemented.baseline_id, review.digest, {D::Local, D::Baidu}, true).isSuccess(), "changed supplement control generation bypassed seal");
        execute(pool, "UPDATE resource_cloud_backfills SET control_generation=0 WHERE run_id='singer-baidu-supplement:baidu'");
        get(baseline.activate(supplemented.baseline_id, review.digest, {D::Local, D::Baidu}, true));
        require(scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE dispatch_state='active'") == 2 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == 0, "activation started work or enabled extra target");
        std::cout << "PASS: independent single-target adoption, missing decision, target revision, baseline review/seal/activation and drift fences\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << stage << ": " << error.what() << '\n';
        return 1;
    }
}
