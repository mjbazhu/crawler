#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceQueueCapacity.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransfer.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"
#include "migu/support/ResourceCapacityLockReadTestSupport.h"
#include <future>
#include <iostream>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
using D = model::ResourceDestination;
int main()
{
    using namespace test;
    try {
        require(c::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_capacity_"), "refusing non-isolated capacity database");
        auto pool = c::tests::migu::support::makeCoverTestPool();
        activate(pool);
        const auto one = singer(pool, 1), two = singer(pool, 2), three = singer(pool, 3), four = singer(pool, 4), five = singer(pool, 5);
        mysql::MySqlResourceQueueCapacity limits(pool);
        mysql::MySqlResourceTransferJobs jobs(pool);
        const auto configured = get(limits.configure(D::Local, 1, 1, 1));
        require(configured.version == 2, "capacity version not advanced");
        const auto a = request(model::ResourceOwner::Singer, one, "capacity-a"), b = request(model::ResourceOwner::Singer, two, "capacity-b");
        auto first = std::async(std::launch::async, [&] { return jobs.enqueue(a, profiles()); });
        auto second = std::async(std::launch::async, [&] { return jobs.enqueue(b, profiles()); });
        const auto x = first.get(), y = second.get();
        require(x.isSuccess() != y.isSuccess(), "concurrent enqueue crossed capacity");
        const auto &rejected = x.isSuccess() ? y : x;
        require(rejected.message() == "resource_queue_full", "full queue lost stable error");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == 1 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == 1 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 1, "full queue left partial operation");
        const auto accepted = x.isSuccess() ? a : b;
        const auto operation = x.isSuccess() ? *x.data() : *y.data();
        require(get(jobs.enqueue(accepted, profiles())).replayed, "full queue rejected idempotent replay");
        auto multi = request(model::ResourceOwner::Singer, three, "capacity-multi", {D::Local, D::Baidu, D::Gdrive});
        require(jobs.enqueue(multi, profiles()).message() == "resource_queue_full", "multi target over capacity accepted");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == 1 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 0, "rejection partially wrote another target");
        model::LegacyResourceTransferRequest legacy_request;
        legacy_request.owner = model::ResourceOwner::Singer;
        legacy_request.cloud_provider = D::Baidu;
        legacy_request.profiles = profiles();
        legacy_request.covers.query.resource_ids = {three};
        require(mysql::MySqlLegacyResourceTransfer(pool).start(legacy_request).message() == "resource_queue_full" && scalar(pool, "SELECT COUNT(*) FROM resource_legacy_batches") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_legacy_profiles") == 0, "legacy full queue left batch or profile mapping");
        execute(pool, "UPDATE singer_resources SET local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='fixture',local_skip_reason='both_saved' WHERE id=" + std::to_string(four));
        const auto skipped = get(jobs.enqueue(request(model::ResourceOwner::Singer, four, "capacity-skip"), profiles()));
        require(skipped.tasks.front().state == "skipped", "history skip consumed capacity");
        require(!limits.configure(D::Local, 1, 2, 2).isSuccess(), "stale capacity version accepted");
        execute(pool, "UPDATE resource_transfer_tasks SET state='uncertain' WHERE operation_id='" + operation.operation_id + "'");
        require(get(limits.status())[0].transfers == 1 && jobs.enqueue(request(model::ResourceOwner::Singer, three, "capacity-uncertain"), profiles()).message() == "resource_queue_full", "uncertain remote work released capacity");
        execute(pool, "UPDATE resource_transfer_tasks SET state='pending' WHERE operation_id='" + operation.operation_id + "'");
        get(jobs.cancel(operation.operation_id, {}));
        require(get(limits.status())[0].transfers == 0, "cancelled queued task retained capacity");
        auto pair = request(model::ResourceOwner::Singer, three, "capacity-pair");
        pair.resources.push_back({model::ResourceOwner::Singer, five});
        require(jobs.enqueue(pair, profiles()).message() == "resource_queue_full" && get(limits.status())[0].transfers == 0, "multi resource failure was partially accepted");
        get(limits.configure(D::Local, 2, 3, 1));
        get(jobs.enqueue(pair, profiles()));
        require(!limits.configure(D::Local, 3, 1, 1).isSuccess() && get(limits.status())[0].transfer_limit == 3, "lowered limit below occupancy");
        execute(pool, "CREATE TRIGGER capacity_audit_fail BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture audit failure'");
        require(!limits.configure(D::Local, 3, 4, 1).isSuccess() && get(limits.status())[0].transfer_limit == 3, "audit failure did not rollback limits");
        execute(pool, "DROP TRIGGER capacity_audit_fail");
        const auto m1 = mrc(pool, 10), m2 = mrc(pool, 11);
        get(limits.configure(D::Baidu, 1, 100, 1));
        auto waiting_profiles = profiles(true);
        waiting_profiles[1].producer_key_available = false;
        const auto p1 = get(jobs.enqueue(request(model::ResourceOwner::Song, m1, "capacity-producer-one", {D::Baidu}), waiting_profiles));
        require(get(limits.status())[1].producers == 1 && scalar(pool, "SELECT COUNT(*) FROM resource_producer_tasks WHERE state='waiting_input'") == 1, "waiting producer not counted");
        const auto before = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(jobs.enqueue(request(model::ResourceOwner::Song, m2, "capacity-producer-two", {D::Baidu}), profiles(true)).message() == "resource_queue_full", "producer capacity ignored");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == before && get(limits.status())[1].transfers == 1, "producer full left file work");
        const auto producer = scalar(pool, "SELECT MIN(producer_id) FROM resource_producer_tasks");
        execute(pool, "UPDATE resource_producer_tasks SET state='failed' WHERE producer_id=" + std::to_string(producer));
        get(jobs.enqueue(request(model::ResourceOwner::Song, m2, "capacity-producer-two", {D::Baidu}), profiles(true)));
        require(mysql::MySqlResourceProducerTasks(pool).retry(producer).message() == "resource_queue_full" && scalar(pool, "SELECT COUNT(*) FROM resource_producer_tasks WHERE state='failed'") == 1, "explicit producer retry exceeded capacity or failed rollback");
        get(limits.configure(D::Baidu, 2, 2, 1));
        mysql::MySqlResourceProducerTasks producers(pool);
        const auto claim = get(producers.claim(D::Baidu, std::string(64, 'b'), "rclone", "fixture-v1", 1, true));
        require(claim.has_value(), "child capacity fixture producer unavailable");
        const model::LyricArtifactBundle bundle {m2, std::string(64, 'f'), "fixture-v1", 1, {{"word_json", {}, 3, std::string(64, 'a')}, {"enhanced_lrc", {}, 4, std::string(64, 'b')}}};
        const auto operations_before_child = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(producers.complete(*claim, bundle, 9).message() == "resource_queue_full" && scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts") == 0 && scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE reference_sha256<>''") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == operations_before_child, "full child queue left artifact, reference or operation writes");
        c::tests::capacity_lock_read::verify(pool);
        std::cout << "PASS: concurrent capacity, atomic rejection, replay, skip, uncertain, cancellation, configuration, producer retry and current locking counts\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
