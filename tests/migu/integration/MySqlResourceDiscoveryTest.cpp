#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/application/use_cases/ResourceDiscoveryWorkflow.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"
#include "../../../src/infrastructure/app/ResourceArchiveCliSupport.h"
#include <atomic>
#include <fstream>
#include <iostream>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
namespace {
// 元数据替身，支持并发与读期间来源变化，不访问真实云盘。
class Probe final : public c::application::ports::ResourceDiscoveryProbe {
public:
    std::string hash = std::string(64, 'b'); // 固定百度目标
    std::atomic<int> calls {0};              // 统计元数据请求
    std::function<void()> mutate;            // 第一次查询时改变来源
    c::application::result::ServiceResult<std::string> fingerprint(const model::ResourceTransferContext &) override
    {
        return c::application::result::ServiceResult<std::string>::success(hash);
    }
    c::application::result::ServiceResult<model::ResourceObjectObservation> observe(std::string path, const model::ResourceTransferContext &context) override
    {
        test::require(context.remaining() > 0 && context.remaining() <= 5000, "object budget not bounded");
        if (calls.fetch_add(1) == 0 && mutate) mutate();
        model::ResourceObjectObservation value;
        value.state = path.ends_with("3") ? "missing" : path.ends_with("4") ? "unknown" :
                                                                              "found";
        if (value.state == "found") {
            value.size_bytes = 3;
            value.hashes["md5"] = std::string(32, 'a');
        }
        return c::application::result::ServiceResult<model::ResourceObjectObservation>::success(value);
    }
};
// 只从真实只读预览捕获定义，后续不会扩大高水位。
model::ResourceDiscoveryDefinition definition(mysql::MySqlLegacyResourceImport &sources, std::string name, model::ResourceOwner owner = model::ResourceOwner::Singer)
{
    const auto page = test::get(sources.preview(owner, 0, 0, 2));
    model::ResourceDiscoveryDefinition d;
    d.run_id = std::move(name);
    d.owner = owner;
    d.high_water_id = page.high_water_id;
    d.database_identity = page.database_identity;
    d.target_fingerprint = page.targets[1].fingerprint;
    d.target_revision = page.targets[1].revision;
    d.page_size = 2;
    d.concurrency = 2;
    d.strip_prefix = "/Archive";
    return d;
}
} // namespace
int main()
{
    using namespace test;
    try {
        require(c::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_discovery_"), "refusing non-isolated discovery database");
        auto pool = c::tests::migu::support::makeCoverTestPool();
        activate(pool);
        for (int i = 1; i <= 6; ++i) {
            const auto id = singer(pool, i);
            execute(pool, "UPDATE singer_resources SET status=2,size_bytes=3,checksum=CONCAT('md5:',REPEAT('a',32)) WHERE id=" + std::to_string(id));
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, id}, {{"cloud_path", i == 6 ? "" : "/Archive/file" + std::to_string(i)}});
        }
        mysql::MySqlResourceDiscovery runs(pool);
        mysql::MySqlLegacyResourceImport sources(pool);
        const auto d = definition(sources, "discovery-fixture");
        get(runs.start(d));
        require(get(runs.start(d)).processed == 0, "start not idempotent");
        auto changed = d;
        changed.concurrency = 1;
        require(!runs.start(changed).isSuccess(), "run id accepted changed scope");
        changed = d;
        changed.run_id = "discovery-other";
        require(!runs.start(changed).isSuccess(), "same target admitted two active runs");
        const auto old = *get(runs.claim(d.run_id));
        require(!get(runs.claim(d.run_id)), "live page lease stolen");
        get(runs.control(d.run_id, "pause"));
        get(runs.control(d.run_id, "resume"));
        const auto newer = *get(runs.claim(d.run_id));
        require(newer.claim_generation > old.claim_generation && !get(runs.current(old)), "pause/resume did not invalidate old page");
        const auto oldPage = get(sources.preview(d.owner, 0, d.high_water_id, 2));
        require(!get(runs.commit(old, oldPage, {})), "late page advanced checkpoint");
        require(!get(runs.release(old, "stale")), "old failure paused new owner");
        get(runs.release(newer, "fixture_pause"));
        get(runs.control(d.run_id, "resume"));
        Probe probe;
        probe.mutate = [&] { execute(pool, "UPDATE singer_resources SET size_bytes=4 WHERE id=(SELECT minimum FROM(SELECT MIN(id) minimum FROM singer_resources) r)"); };
        c::application::use_cases::ResourceDiscoveryWorkflow workflow(runs, sources, probe);
        auto progress = get(workflow.step(d.run_id));
        require(progress.processed == 2 && progress.counts.at("source_changed") == 1 && progress.counts.at("hash_matched") == 1, "source drift or comparable hash was lost");
        singer(pool, 7);
        require(get(runs.start(d)).definition.high_water_id == d.high_water_id, "new data enlarged historical scope");
        const auto cursor = progress.cursor;
        execute(pool, "CREATE TRIGGER discovery_insert_fail BEFORE INSERT ON resource_discovery_observations FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture failure'");
        require(!workflow.step(d.run_id).isSuccess() && get(runs.find(d.run_id)).cursor == cursor && scalar(pool, "SELECT COUNT(*) FROM resource_discovery_observations") == 2, "failed page left observations or cursor");
        execute(pool, "DROP TRIGGER discovery_insert_fail");
        execute(pool, "UPDATE resource_discovery_runs SET lease_until=UNIX_TIMESTAMP()-1 WHERE run_id='discovery-fixture'");
        progress = get(workflow.step(d.run_id));
        require(progress.processed == 4 && progress.counts.at("missing") == 1 && progress.counts.at("unknown") == 1, "lease recovery collapsed missing and unknown");
        probe.hash = std::string(64, 'c');
        require(!workflow.step(d.run_id).isSuccess() && get(runs.find(d.run_id)).state == "paused" && get(runs.find(d.run_id)).cursor == progress.cursor, "target drift advanced cursor");
        probe.hash = d.target_fingerprint;
        get(runs.control(d.run_id, "resume"));
        progress = get(workflow.step(d.run_id));
        require(progress.state == "completed" && progress.cursor == d.high_water_id && progress.processed == 6 && progress.counts.at("unresolved") == 1 && probe.calls == 7, "high water/path skip or completion incorrect");
        require(!runs.control(d.run_id, "resume").isSuccess(), "completed run revived");
        const auto observations = get(runs.observations(d.run_id, 0, progress.cursor, 500));
        require(observations.size() == 6 && observations.front().grade == "source_changed", "checkpoint observations not persistent");
        c::tests::cloud_transfer::Root output;
        const auto report = output.path / "discovery-report";
        c::infrastructure::app::archive_cli::exportReport(runs, progress, c::tests::cloud_transfer::utf8(report));
        std::ifstream input(report / "report.jsonl");
        std::string line;
        std::getline(input, line);
        require(nlohmann::json::parse(line).at("format") == "crawler.resource-cloud-discovery.v1", "report uses import attestation format");
        bool refused = false;
        try {
            c::infrastructure::app::archive_cli::exportReport(runs, progress, c::tests::cloud_transfer::utf8(report));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        require(refused, "report overwrote existing output");
        changed = definition(sources, "discovery-cancel");
        get(runs.start(changed));
        get(runs.control(changed.run_id, "cancel"));
        require(!get(runs.claim(changed.run_id)) && !runs.control(changed.run_id, "resume").isSuccess(), "cancelled discovery resumed");
        const auto resource = mrc(pool, 20);
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version) VALUES(" + std::to_string(resource) + ",'word_json',REPEAT('a',64),'fixture',1)");
        model::AlbumSourceMetadata album;
        album.source_album_id = "discovery-album";
        album.title = "fixture";
        album.cover_url = "https://example.test/cover.png";
        model::MetadataWriteBatch batch;
        batch.albums = {album};
        get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
        for (auto owner : {model::ResourceOwner::Song, model::ResourceOwner::Album, model::ResourceOwner::SongArtifact}) {
            const auto ownerDef = definition(sources, "discovery-" + std::string(model::resourceOwnerName(owner)), owner);
            get(runs.start(ownerDef));
            require(get(workflow.step(ownerDef.run_id)).state == "completed", "unified owner discovery incomplete");
        }
        changed = definition(sources, "discovery-revision");
        get(runs.start(changed));
        const auto lease = *get(runs.claim(changed.run_id));
        execute(pool, "UPDATE resource_destination_states SET target_revision=2 WHERE destination='baidu'");
        require(!runs.current(lease).isSuccess() && !runs.commit(lease, oldPage, {}).isSuccess(), "target revision did not fence page");
        get(runs.control(changed.run_id, "cancel"));
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 0 && scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE local_skip_generation>0") == 0, "read only discovery changed transfer facts");
        std::cout << "PASS: discovery leases, pause/cancel, drift, atomic checkpoints, recovery, four owners, immutable report and no transfer writes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
