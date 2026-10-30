#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferAccess.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"
#include <iostream>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace {
// 所有诊断为固定文本，不回显连接或历史路径。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 测试仅使用显式创建的隔离库。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "baseline fixture");
}
// 返回已验证的持久结果。
template <class T>
T get(c::application::result::ServiceResult<T> result)
{
    if (!result.isSuccess()) throw std::runtime_error(result.message());
    return *result.data();
}
// 有界标量，用于验证事务副作用。
std::uint64_t count(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    auto value = execute(pool, std::move(sql));
    return mysql::MySqlValueReader::uint64At(value.rows().front(), 0);
}
} // namespace
// 检查四类完整性、审阅后变化、排除策略与原子放行；不连接任何内容端口。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_baseline_"), "refusing non-isolated baseline database");
        auto pool = support::makeCoverTestPool();
        require(count(pool, "SELECT COUNT(*) FROM songs") == 0, "requires empty baseline fixture");
        mysql::MySqlLegacyResourceImport importer(pool);
        std::vector<model::ResourceTransferTargetProfile> profiles;
        for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            const auto fingerprint = std::string(64, char('a' + static_cast<int>(destination)));
            get(importer.configureTarget(destination, 1, fingerprint));
            model::ResourceTransferTargetProfile profile;
            profile.destination = destination;
            profile.fingerprint = fingerprint;
            profile.adapter = destination == model::ResourceDestination::Local ? "filesystem" : destination == model::ResourceDestination::Baidu ? "baidupcs" :
                                                                                                                                                   "rclone";
            profiles.push_back(profile);
        }
        model::MetadataWriteBatch batch;
        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "baseline-song";
        song.source_content_id = "baseline-content";
        song.title = "基线测试";
        song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
        batch.songs = {song};
        model::AlbumSourceMetadata album;
        album.source_album_id = "baseline-album";
        album.title = "未声明双端的历史封面";
        album.cover_url = "https://example.test/baseline-cover.png";
        batch.albums = {album};
        require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch).isSuccess(), "fixture metadata failed");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'mrc','' FROM song_source_refs ON DUPLICATE KEY UPDATE id=song_resources.id");
        execute(pool, "UPDATE song_resources SET status=2,local_path='missing/fixture',size_bytes=3,checksum='rclone:size:3' WHERE resource_kind='audio'");
        execute(pool, "UPDATE album_resources SET status=2,local_path='missing/album-fixture',size_bytes=3");
        const auto blocked = count(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        const auto imported = count(pool, "SELECT id FROM song_resources WHERE resource_kind='audio' LIMIT 1");
        model::ResourceBaselinePlan plan;
        plan.baseline_id = "fixture-baseline";
        for (std::size_t i = 0; i < 4; ++i) {
            plan.import_runs[i] = "baseline-run-" + std::to_string(i);
            model::LegacyResourceImportPlan import_plan {plan.import_runs[i], std::string(64, char('a' + i)), true, {}};
            import_plan.operator_attested_both = i == 0;
            import_plan.operator_excluded_all = i != 0;
            auto page = get(importer.preview(static_cast<model::ResourceOwner>(i), 0, 0, 500));
            auto ambiguous = import_plan;
            ambiguous.operator_attested_both = ambiguous.operator_excluded_all = true;
            require(!importer.importPage(ambiguous, page).isSuccess(), "ambiguous import policy was accepted");
            const auto applied = get(importer.importPage(import_plan, page));
            require(applied.conflicts == 0, "baseline import had conflicts");
        }
        mysql::MySqlResourceBaseline baseline(pool);
        auto missing = plan;
        missing.import_runs[3] = "missing-run";
        require(!baseline.review(missing).isSuccess(), "missing owner scope accepted");
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,album_resource_id,task_state) SELECT 'album',id,'baidu',id,'uncertain' FROM album_resources");
        require(!baseline.review(plan).isSuccess(), "unified uncertain cloud object allowed baseline review");
        execute(pool, "DELETE FROM resource_cloud_receipts WHERE owner_type='album' AND task_state='uncertain'");
        const auto reviewed = get(baseline.review(plan));
        require(reviewed.resources >= 2 && reviewed.blocked_resources > 0, "review missed excluded resources");
        require(count(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='album'") == 0, "excluded completed cover fabricated a cloud receipt");
        require(count(pool, "SELECT COUNT(*) FROM resource_cloud_baselines") == 0 && count(pool, "SELECT COUNT(*) FROM resource_baseline_blocks") == 0, "review wrote persistent state");
        require(!baseline.seal(plan, reviewed.digest).isSuccess(), "seal omitted writer stop confirmation");
        plan.writers_stopped = true;
        require(!baseline.seal(plan, std::string(64, 'f')).isSuccess(), "wrong reviewed digest sealed");
        require(count(pool, "SELECT COUNT(*) FROM resource_baseline_blocks") == 0, "failed seal leaked block rows");
        execute(pool, "UPDATE song_resources SET size_bytes=4 WHERE id=" + std::to_string(imported));
        require(!baseline.seal(plan, reviewed.digest).isSuccess(), "changed resource sealed old review");
        execute(pool, "UPDATE song_resources SET size_bytes=3 WHERE id=" + std::to_string(imported));
        const auto sealed = get(baseline.seal(plan, reviewed.digest));
        require(sealed.state == "sealed" && count(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE dispatch_state='maintenance'") == 3, "seal activated dispatch");
        get(baseline.seal(plan, reviewed.digest));
        require(count(pool, "SELECT COUNT(*) FROM resource_baseline_blocks") == reviewed.blocked_resources, "seal replay duplicated blocks");
        require(count(pool, "SELECT COUNT(*) FROM resource_baseline_blocks WHERE owner_type='album'") == 1, "completed excluded cover was not blocked");
        execute(pool, "UPDATE resource_destination_states SET target_fingerprint=REPEAT('e',64) WHERE destination='baidu'");
        require(!baseline.activate(plan.baseline_id, reviewed.digest, {model::ResourceDestination::Local}, true).isSuccess(), "target change activated old baseline");
        execute(pool, "UPDATE resource_destination_states SET target_fingerprint=REPEAT('b',64) WHERE destination='baidu'");
        const auto active = get(baseline.activate(plan.baseline_id, reviewed.digest, {model::ResourceDestination::Local, model::ResourceDestination::Baidu}, true));
        require(active.state == "active" && count(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE baseline_ready=1 AND dispatch_state='active'") == 2 && count(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE destination='gdrive' AND dispatch_state='paused'") == 1, "activation did not preserve selected targets");
        get(baseline.activate(plan.baseline_id, reviewed.digest, {model::ResourceDestination::Local, model::ResourceDestination::Baidu}, true));
        require(!baseline.maintenance(plan.baseline_id, reviewed.digest, false).isSuccess(), "maintenance omitted writer stop confirmation");
        require(!baseline.maintenance(plan.baseline_id, std::string(64, 'f'), true).isSuccess(), "maintenance accepted stale digest");
        execute(pool, "CREATE TRIGGER fixture_maintenance_audit BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture_audit'");
        require(!baseline.maintenance(plan.baseline_id, reviewed.digest, true).isSuccess(), "maintenance ignored audit failure");
        require(count(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE baseline_ready=1") == 3, "maintenance audit failure did not roll back gates");
        execute(pool, "DROP TRIGGER fixture_maintenance_audit");
        require(get(baseline.maintenance(plan.baseline_id, reviewed.digest, true)).state == "maintenance", "maintenance transition failed");
        require(count(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE dispatch_state='maintenance' AND baseline_ready=0 AND baseline_manifest=''") == 3, "maintenance did not close all gates");
        execute(pool, "UPDATE resource_cloud_baselines SET state='active'");
        require(!baseline.maintenance(plan.baseline_id, reviewed.digest, true).isSuccess(), "inconsistent maintenance state was accepted");
        execute(pool, "UPDATE resource_cloud_baselines SET state='sealed'");
        get(baseline.maintenance(plan.baseline_id, reviewed.digest, true));
        require(count(pool, "SELECT COUNT(*) FROM governance_events WHERE action='resource.baseline.maintenance'") == 1, "maintenance replay duplicated audit");
        require(count(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE transfer_policy='skip'") > 0, "maintenance erased historical facts");
        get(baseline.activate(plan.baseline_id, reviewed.digest, {model::ResourceDestination::Local, model::ResourceDestination::Baidu}, true));
        mysql::MySqlResourceTransferAccess access(pool);
        const auto excluded = get(access.inspect({model::ResourceOwner::Song, blocked}, model::ResourceDestination::Local));
        require(!excluded.dispatch_ready && excluded.blocked_code == "historical_scope_excluded", "excluded history became downloadable");
        require(get(access.inspect({model::ResourceOwner::Song, imported}, model::ResourceDestination::Local)).skip.enabled, "activation removed imported skip");
        mysql::MySqlResourceTransferJobs jobs(pool);
        model::ResourceTransferRequest request;
        request.resources = {{model::ResourceOwner::Song, blocked}};
        request.destinations = {model::ResourceDestination::Local};
        require(get(jobs.enqueue(request, profiles)).tasks.front().result_code == "historical_scope_excluded", "new task bypassed baseline exclusion");
        mysql::MySqlSongResourceQueue::SqlPaths paths;
        paths.enforce_dispatch = true;
        mysql::MySqlSongResourceQueue legacy(pool, paths);
        model::SongResourceClaimQuery claim;
        claim.source = "migu";
        claim.limit = 100;
        require(legacy.claimPending(claim).empty(), "legacy queue bypassed baseline exclusion or skip");
        request.action = model::ResourceTransferAction::RefreshContent;
        require(get(jobs.enqueue(request, profiles)).tasks.front().state == "pending", "explicit fresh content could not leave old-generation block");
        std::cout << "PASS: baseline complete scopes, review changes, exclusions, activation and replay\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "baseline test failed: " << error.what() << '\n';
        return 1;
    }
}
