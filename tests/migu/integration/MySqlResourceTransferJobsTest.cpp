#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceWriteInspector.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"
#include "migu/support/ResourceTransferExecutionTestSupport.h"
#include "migu/support/ResourceTransferTimeoutTestSupport.h"
#include "migu/support/ResourcePreparedAdmissionTestSupport.h"

#include <algorithm>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
using Destination = model::ResourceDestination;
using Action = model::ResourceTransferAction;

namespace {
// 固定失败说明不输出数据库连接信息。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 结果失败时仍只输出适配器的固定脱敏消息。
template <typename T>
T get(c::application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 仅在显式隔离库中执行 fixture。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(statement)), "transfer jobs fixture");
}
// 固定计数查询。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    const auto rows = execute(pool, std::move(statement));
    require(!rows.rows().empty(), "missing scalar");
    return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 三个互不相同的固定目标，无凭据或真实根。
std::vector<model::ResourceTransferTargetProfile> profiles()
{
    std::vector<model::ResourceTransferTargetProfile> values;
    for (const auto destination : {Destination::Local, Destination::Baidu, Destination::Gdrive}) {
        model::ResourceTransferTargetProfile value;
        value.destination = destination;
        value.adapter = destination == Destination::Local ? "filesystem" : destination == Destination::Baidu ? "baidupcs" :
                                                                                                               "rclone";
        value.fingerprint = std::string(64, char('a' + static_cast<int>(destination)));
        values.push_back(value);
    }
    return values;
}
// 指定资源和可选目标，缺省目标留给第一次受理配置。
model::ResourceTransferRequest request(model::ResourceIdentity identity, std::vector<Destination> destinations = {}, Action action = Action::Process)
{
    model::ResourceTransferRequest value;
    value.resources = {identity};
    value.destinations = std::move(destinations);
    value.action = action;
    return value;
}
// 必须返回真正认领的本端任务。
model::ResourceTransferJob claimed(mysql::MySqlResourceTransferJobs &jobs, Destination destination, const std::vector<model::ResourceTransferTargetProfile> &targets)
{
    const auto &profile = targets.at(static_cast<std::size_t>(destination));
    auto value = get(jobs.claim(destination, profile.fingerprint, profile.adapter));
    require(value.has_value(), "expected independently claimable task");
    return *value;
}
// 测试使用显式伪造的存储证据，只验证仓储条件，不声称执行真实核验。
model::ResourceTransferReceipt receipt(const model::ResourceTransferJob &job, std::string path)
{
    model::ResourceTransferReceipt value;
    value.path = std::move(path);
    value.checksum = std::string(64, 'a');
    value.size_bytes = 3;
    value.adapter = job.adapter;
    value.verification = model::ResourceVerification::Sha256Verified;
    value.verification_method = job.destination == Destination::Local ? "local_sha256" : "remote_sha256";
    if (job.destination != Destination::Local) {
        value.remote_hash_algorithm = "sha256";
        value.remote_hash_value = value.checksum;
    }
    return value;
}
} // namespace

// 独立 MySQL 验证原子三路、幂等、占用、版本、取消、未决修复与重启；不调用内容端口。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_transfer_jobs_"), "refusing non-isolated transfer jobs database");
        auto pool = support::makeCoverTestPool();
        require(scalar(pool, "SELECT COUNT(*) FROM songs") == 0, "requires empty fixture database");
        require(mysql::MySqlMusicCatalogQueryRepository(pool).schemaStatus().ok, "latest jobs schema not ready");
        model::MetadataWriteBatch batch;
        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "jobs-song";
        song.source_content_id = "jobs-content";
        song.copyright_id = "fixture-copyright";
        song.source_resource_type = "2";
        song.title = "三路任务测试";
        song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
        batch.songs = {song};
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "jobs-singer";
        singer.name = "任务歌手";
        singer.avatar_url = "https://example.test/singer.png";
        batch.singers = {singer};
        model::AlbumSourceMetadata album;
        album.source_album_id = "jobs-album";
        album.title = "任务专辑";
        album.source_album_kind = 1;
        album.source_resource_type = "2003";
        album.cover_url = "https://example.test/album.png";
        batch.albums = {album};
        require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch).isSuccess(), "metadata fixture failed");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'mrc','' FROM song_source_refs WHERE source_content_id='jobs-content' ON DUPLICATE KEY UPDATE id=song_resources.id");
        const auto audio = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='audio' LIMIT 1");
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version,status,local_path,checksum,size_bytes) SELECT id,'word_json','old-source','fixture',1,2,'missing/artifact.json','old',3 FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        const std::vector<std::pair<std::string, std::string>> owners = {{"song_resources", "song"}, {"singer_resources", "singer"}, {"album_resources", "album"}, {"song_resource_artifacts", "song_artifact"}};
        model::ResourceTransferRequest historical;
        for (const auto &[table, owner] : owners) {
            const auto id = table == "song_resources" ? audio : scalar(pool, "SELECT id FROM " + table + " LIMIT 1");
            historical.resources.push_back({*model::parseResourceOwner(owner), id});
            execute(pool, "UPDATE " + table + " SET local_skip_generation=1,local_skip_target_revision=1,local_skip_baseline='reviewed',local_skip_reason='legacy_both',status=2");
            const auto fk = owner == "song" ? "song_resource_id" : owner == "singer" ? "singer_resource_id" :
                                                               owner == "album"      ? "album_resource_id" :
                                                                                       "artifact_resource_id";
            for (const auto provider : {"baidu", "gdrive"})
                execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + std::string(fk) + ",transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason) VALUES('" + owner + "'," + std::to_string(id) + ",'" + provider + "'," + std::to_string(id) + ",'skip',1,1,'reviewed','legacy_both')");
        }
        mysql::MySqlResourceTransferJobs jobs(pool);
        require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch).isSuccess(), "no-op metadata upsert lost source identity");
        auto targets = profiles(), offline = targets;
        for (auto &profile : offline) profile.available = false;
        historical.idempotency_key = "historical-no-content";
        const auto skipped = get(jobs.enqueue(historical, offline));
        require(skipped.state == "completed" && skipped.tasks.size() == 12 && std::all_of(skipped.tasks.begin(), skipped.tasks.end(), [](const auto &item) { return item.state == "skipped" && item.result_code == "skip_existing"; }), "four-owner history did not finish without online targets");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 0, "skip reserved transfer slots");
        mysql::MySqlResourceTransferQueries query_repository(pool);
        require(get(query_repository.compatible()), "v2 queries require compatible schema");
        model::ResourceCloudReceiptQuery page_query;
        page_query.resource_id = audio;
        page_query.limit = 1;
        const auto history_page = get(query_repository.receipts(page_query));
        require(history_page.items.size() == 1, "history receipt page size incorrect");
        require(history_page.next_after_id.has_value(), "history receipt next cursor missing");
        require(history_page.items.front().transfer_skipped, "history receipt lost current skip policy");
        require(!history_page.items.front().current && !history_page.items.front().uploaded_at, "history query fabricated current or upload evidence");
        page_query.after_id = *history_page.next_after_id;
        const auto second_page = get(query_repository.receipts(page_query));
        require(second_page.items.size() == 1 && !second_page.next_after_id && second_page.items.front().id != history_page.items.front().id, "receipt cursor repeated or lost target");
        for (const auto &profile : targets) require(!get(jobs.claim(profile.destination, profile.fingerprint, profile.adapter)), "skip became claimable");

        const auto singer_id = historical.resources[1].resource_id, album_id = historical.resources[2].resource_id, artifact_id = historical.resources[3].resource_id;
        const auto singer_revision = scalar(pool, "SELECT source_revision FROM singer_resources WHERE id=" + std::to_string(singer_id));
        execute(pool, "UPDATE singer_resources SET local_path='legacy/confirmed.png',size_bytes=3,checksum=REPEAT('f',64),local_content_generation=0,status=1,running_batch_id='legacy-owner' WHERE id=" + std::to_string(singer_id));
        execute(pool, "UPDATE singer_resources SET remote_url='https://example.test/changed-singer.png' WHERE id=" + std::to_string(singer_id));
        require(scalar(pool, "SELECT source_revision FROM singer_resources WHERE id=" + std::to_string(singer_id)) == singer_revision + 1, "cover URL did not fence old source");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE id=" + std::to_string(singer_id) + " AND status=2 AND local_path='legacy/confirmed.png' AND local_content_generation=0") == 1, "source change discarded legacy confirmed local fact");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE id=" + std::to_string(singer_id) + " AND content_generation=1 AND local_skip_generation=1") == 1, "cover URL refresh revoked historical skip");
        execute(pool, "UPDATE singer_resources SET remote_url=remote_url WHERE id=" + std::to_string(singer_id));
        require(scalar(pool, "SELECT source_revision FROM singer_resources WHERE id=" + std::to_string(singer_id)) == singer_revision + 1, "no-op cover metadata advanced source revision");
        execute(pool, "UPDATE singer_resources SET remote_url='https://example.test/Changed-singer.png' WHERE id=" + std::to_string(singer_id));
        require(scalar(pool, "SELECT source_revision FROM singer_resources WHERE id=" + std::to_string(singer_id)) == singer_revision + 2, "case-sensitive URL change hidden by table collation");
        execute(pool, "UPDATE album_resources SET remote_url='https://example.test/changed-album.png' WHERE id=" + std::to_string(album_id));
        require(get(jobs.enqueue(request({model::ResourceOwner::Album, album_id}, {Destination::Local}), offline)).tasks.front().result_code == "skip_existing", "descriptor change caused historical cover download");
        const auto song_revision = scalar(pool, "SELECT source_revision FROM song_resources WHERE id=" + std::to_string(audio));
        execute(pool, "UPDATE song_source_refs SET copyright_id='fixture-copyright-enriched' WHERE source_content_id='jobs-content'");
        execute(pool, "UPDATE song_source_audio_formats SET aformat='020099' WHERE song_source_ref_id=(SELECT id FROM song_source_refs WHERE source_content_id='jobs-content')");
        require(scalar(pool, "SELECT source_revision FROM song_resources WHERE id=" + std::to_string(audio)) == song_revision + 2, "song metadata/format did not capture source revision");
        require(get(jobs.enqueue(request({model::ResourceOwner::Song, audio}, {Destination::Local}), offline)).tasks.front().result_code == "skip_existing", "source enrichment revoked song history");
        execute(pool, "UPDATE song_resource_artifacts SET processor_version='fixture-v2',schema_version=2 WHERE id=" + std::to_string(artifact_id));
        require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE id=" + std::to_string(artifact_id) + " AND content_generation=2 AND local_skip_generation=1") == 1, "artifact producer change reused old content generation");
        require(get(jobs.enqueue(request({model::ResourceOwner::SongArtifact, artifact_id}, {Destination::Local}), offline)).tasks.front().result_code != "skip_existing", "new artifact reused old generation skip");

        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64),target_fingerprint=CASE destination WHEN 'local' THEN REPEAT('a',64) WHEN 'baidu' THEN REPEAT('b',64) ELSE REPEAT('c',64) END");
        execute(pool, "UPDATE song_resources SET status=0,local_skip_generation=0,local_skip_target_revision=0 WHERE id=" + std::to_string(audio));
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='normal' WHERE owner_type='song' AND resource_id=" + std::to_string(audio));
        auto all = request({model::ResourceOwner::Song, audio});
        all.idempotency_key = "same-three-way-request";
        auto rclone_targets = targets;
        rclone_targets[1].adapter = "rclone";
        auto accepted = get(jobs.enqueue(all, rclone_targets));
        require(accepted.tasks.size() == 3 && accepted.state == "running", "three destinations not registered atomically");
        require(get(jobs.enqueue(all, offline)).operation_id == accepted.operation_id, "replay expanded changed profiles");
        require(get(jobs.enqueue(all, targets)).tasks[1].adapter == "rclone", "idempotent replay changed frozen adapter");
        require(get(jobs.enqueue(all, {})).operation_id == accepted.operation_id, "disabled current configuration blocked durable idempotency replay");
        const auto operation_count = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(jobs.enqueue(request({model::ResourceOwner::Song, audio}, {Destination::Baidu}), {}).message() == "resource_destination_disabled" && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == operation_count, "disabled destination left a partially accepted operation");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance'");
        require(get(jobs.enqueue(all, {})).operation_id == accepted.operation_id, "maintenance blocked a frozen idempotent operation");
        auto fresh_maintenance = request({model::ResourceOwner::Song, audio}, {Destination::Baidu}, Action::RefreshContent);
        fresh_maintenance.idempotency_key = "fresh-maintenance-request";
        require(jobs.enqueue(fresh_maintenance, {}).message() == "resource_destination_disabled" && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == operation_count && scalar(pool, "SELECT content_generation FROM song_resources WHERE id=" + std::to_string(audio)) == 1, "maintenance replay fallback persisted a fresh operation or refreshed content");
        fresh_maintenance.require_write_admission = true;
        require(jobs.enqueue(fresh_maintenance, targets).message() == "resource_maintenance" && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == operation_count && scalar(pool, "SELECT content_generation FROM song_resources WHERE id=" + std::to_string(audio)) == 1, "maintenance race accepted an operation or refreshed content");
        auto guarded_replay = all;
        guarded_replay.require_write_admission = true;
        require(get(jobs.enqueue(guarded_replay, targets)).operation_id == accepted.operation_id, "atomic admission blocked an existing frozen replay");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active'");
        auto changed = all;
        changed.destinations = {Destination::Baidu};
        require(jobs.enqueue(changed, targets).error() == c::application::result::ServiceErrorType::Conflict, "same key accepted different request");
        auto busy = get(jobs.enqueue(request({model::ResourceOwner::Song, audio}), targets));
        require(std::all_of(busy.tasks.begin(), busy.tasks.end(), [](const auto &item) { return item.state == "busy" && item.busy_task_id > 0; }), "cross-operation ownership was stolen");
        require(get(jobs.cancel(busy.operation_id, {})), "busy operation cancellation failed");
        mysql::MySqlSongResourceQueue::SqlPaths paths;
        paths.enforce_dispatch = true;
        mysql::MySqlSongResourceQueue legacy(pool, paths);
        model::SongResourceClaimQuery old_query;
        old_query.source = "migu";
        old_query.limit = 20;
        const auto old_claim = legacy.claimPending(old_query);
        require(std::none_of(old_claim.begin(), old_claim.end(), [audio](const auto &item) { return item.resource_id == audio; }), "legacy queue claimed pending slot");
        // 清理本用例由旧队列认领的其他 fixture，不触碰持久槽里的资源。
        execute(pool, "UPDATE song_resources SET status=0,running_batch_id=NULL WHERE id<>" + std::to_string(audio) + " AND status=1");
        auto local_future = std::async(std::launch::async, [&] { return claimed(jobs, Destination::Local, targets); });
        auto baidu_future = std::async(std::launch::async, [&] { return claimed(jobs, Destination::Baidu, rclone_targets); });
        auto google_future = std::async(std::launch::async, [&] { return claimed(jobs, Destination::Gdrive, targets); });
        auto local = local_future.get(), baidu = baidu_future.get(), google = google_future.get();
        require(local.operation_id == accepted.operation_id && baidu.operation_id == accepted.operation_id && google.operation_id == accepted.operation_id, "branches did not belong to same operation");
        require(get(jobs.beginAttempt(baidu, {"objects/baidu-attempt", std::string(64, 'a'), 3})), "cloud could not bind reference before local completion");
        require(get(jobs.beginAttempt(local, {"objects/local-attempt", std::string(64, 'a'), 3})), "local rejected matching reference");
        require(!get(jobs.beginAttempt(google, {"objects/google-conflict", std::string(64, 'd'), 3})), "mismatched bytes bypassed reference");
        auto baidu_receipt = receipt(baidu, "objects/baidu-attempt");
        baidu_receipt.verification = model::ResourceVerification::HashMatched;
        baidu_receipt.verification_method = "remote_md5";
        baidu_receipt.remote_hash_algorithm = "md5";
        baidu_receipt.remote_hash_value = std::string(32, 'c');
        baidu_receipt.adapter = "baidupcs";
        require(jobs.complete(baidu, baidu_receipt).error() == c::application::result::ServiceErrorType::InvalidParam, "native Baidu MD5 was accepted as complete content evidence");
        baidu_receipt.adapter = "rclone";
        require(get(jobs.complete(baidu, baidu_receipt)), "cloud completion depended on local");
        page_query.after_id = 0;
        page_query.provider = Destination::Baidu;
        const auto confirmed_page = get(query_repository.receipts(page_query));
        require(confirmed_page.items.size() == 1 && confirmed_page.items.front().adapter == "rclone" && confirmed_page.items.front().current && !confirmed_page.items.front().matches_local && !confirmed_page.items.front().uploaded_at && confirmed_page.items.front().verified_at, "rclone receipt query lost evidence or guessed local match");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(audio) + " AND local_content_generation=0 AND reference_sha256=REPEAT('a',64)") == 1, "cloud changed local receipt or failed to bind content");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + std::to_string(audio) + " AND cloud_provider='baidu' AND source_adapter='rclone' AND verification='hash_matched' AND uploaded_at IS NULL AND verified_at>0") == 1, "rclone Baidu receipt was misclassified or promoted");
        auto strict_targets = rclone_targets;
        strict_targets[1].policy.strict_sha256 = true;
        const auto strict_blocked = get(jobs.enqueue(request(baidu.resource, {Destination::Baidu}), strict_targets));
        require(strict_blocked.tasks.front().result_code == "verification_required", "strict policy silently accepted weak existing receipt");
        const auto strict_operation = get(jobs.enqueue(request(baidu.resource, {Destination::Baidu}, Action::Repair), strict_targets));
        require(strict_operation.tasks.front().attempt_path == "objects/baidu-attempt", "strict repair lost known object and would acquire source again");
        const auto strict_job = claimed(jobs, Destination::Baidu, strict_targets);
        require(get(jobs.complete(strict_job, receipt(strict_job, "objects/baidu-attempt"))), "strict read-only repair could not upgrade exact object evidence");
        require(get(jobs.cancel(accepted.operation_id, {Destination::Local})) && get(jobs.shouldStop(local)), "local cancellation did not stop only local");
        require(get(jobs.fail(local, {"cancelled", "resource", false, false})), "definite pre-publication cancellation was not settled");
        require(!get(jobs.complete(local, receipt(local, "objects/local-attempt"))), "cancelled local completed");
        require(get(jobs.find(accepted.operation_id)).state == "partial", "failed sibling hidden in root state");
        auto cancelled = get(jobs.enqueue(request(local.resource, {Destination::Local}), targets));
        require(cancelled.tasks.front().result_code == "cancelled_policy", "ordinary process cleared cancelled policy");
        auto retried = get(jobs.enqueue(request(local.resource, {Destination::Local}, Action::Retry), targets));
        local = claimed(jobs, Destination::Local, targets);
        require(get(jobs.fail(local, {"network_failed", "network", false, true})), "transient failure not recorded");
        const auto retry_task_id = local.task_id;
        execute(pool, "UPDATE resource_transfer_tasks SET available_at=UNIX_TIMESTAMP()+3600 WHERE task_id=" + std::to_string(retry_task_id));
        require(!get(jobs.claim(Destination::Local, targets.front().fingerprint, targets.front().adapter)), "retry ignored its backoff deadline");
        execute(pool, "UPDATE resource_transfer_tasks SET available_at=UNIX_TIMESTAMP()-1 WHERE task_id=" + std::to_string(retry_task_id));
        const auto pending_resource = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        execute(pool, "UPDATE song_resources SET status=0,local_skip_generation=0,local_skip_target_revision=0 WHERE id=" + std::to_string(pending_resource));
        auto pending = get(jobs.enqueue(request({model::ResourceOwner::Song, pending_resource}, {Destination::Local}), targets));
        local = claimed(jobs, Destination::Local, targets);
        require(local.task_id == retry_task_id, "new pending task starved an eligible retry");
        require(local.attempt_count == 2 && get(jobs.fail(local, {"network_failed", "network", false, true})), "frozen retry budget not used");
        require(get(jobs.find(retried.operation_id)).tasks.front().state == "failed", "retry exceeded frozen limit");
        auto spare = claimed(jobs, Destination::Local, targets);
        require(spare.operation_id == pending.operation_id && get(jobs.fail(spare, {"fixture_done", "resource", false, false})), "pending task lost its queue position");

        auto target_change = get(jobs.enqueue(request(google.resource, {Destination::Gdrive}, Action::Retry), targets));
        google = claimed(jobs, Destination::Gdrive, targets);
        auto wrong_adapter = receipt(google, "objects/invalid-native-google");
        wrong_adapter.adapter = "baidupcs";
        require(jobs.complete(google, wrong_adapter).error() == c::application::result::ServiceErrorType::InvalidParam, "BaiduPCS receipt was accepted for Google");
        require(get(jobs.beginAttempt(google, {"objects/old-target", std::string(64, 'a'), 3})), "target attempt failed");
        execute(pool, "UPDATE resource_destination_states SET target_revision=2,target_fingerprint=REPEAT('d',64) WHERE destination='gdrive'");
        require(!get(jobs.complete(google, receipt(google, "objects/old-target"))), "old target confirmed after switch");
        targets[2].fingerprint = std::string(64, 'd');
        auto uncertain = get(jobs.enqueue(request(google.resource, {Destination::Gdrive}, Action::Retry), targets));
        google = claimed(jobs, Destination::Gdrive, targets);
        require(get(jobs.beginAttempt(google, {"objects/uncertain", std::string(64, 'a'), 3})), "uncertain attempt missing");
        require(get(jobs.fail(google, {"upload_unknown", "persistence", true, false})), "uncertain failure not preserved");
        mysql::MySqlResourceTransferJobs restarted(pool);
        require(get(restarted.find(uncertain.operation_id)).tasks.front().state == "uncertain", "restart lost unresolved attempt");
        auto prevented = get(restarted.enqueue(request(google.resource, {Destination::Gdrive}), targets));
        require(prevented.tasks.front().result_code == "reconcile_required", "ordinary request redownloaded unresolved object");
        auto repair = get(restarted.enqueue(request(google.resource, {Destination::Gdrive}, Action::Repair), targets));
        require(repair.tasks.front().attempt_path == "objects/uncertain", "repair discarded old object identity");
        for (const std::string state : {"pending", "retry"}) {
            execute(pool, "UPDATE resource_transfer_tasks SET state='" + state + "' WHERE task_id=" + std::to_string(repair.tasks.front().task_id));
            require(get(restarted.cancel(repair.operation_id, {Destination::Gdrive})), "unclaimed repair cancellation failed");
            const auto stopped = get(restarted.find(repair.operation_id)).tasks.front();
            require(stopped.state == "uncertain" && stopped.cancel_requested && stopped.result_code == "reconcile_required" && stopped.attempt_path == "objects/uncertain", "unclaimed repair cancellation discarded known side effect");
            require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots WHERE task_id=" + std::to_string(stopped.task_id)) == 1, "unclaimed repair cancellation released unresolved slot");
            const auto blocked = get(restarted.enqueue(request(google.resource, {Destination::Gdrive}, Action::Retry), targets));
            require(blocked.tasks.front().state == "busy" && blocked.tasks.front().busy_task_id == stopped.task_id, "cancelled repair allowed a duplicate upload");
            repair = get(restarted.enqueue(request(google.resource, {Destination::Gdrive}, Action::Repair), targets));
            require(repair.tasks.front().attempt_path == "objects/uncertain", "explicit repair lost cancelled predecessor object");
        }
        const auto repaired = claimed(restarted, Destination::Gdrive, targets);
        require(get(restarted.complete(repaired, receipt(repaired, "objects/uncertain"))), "repair could not confirm exact known object");
        require(!get(jobs.complete(google, receipt(google, "objects/uncertain"))), "old worker confirmed after repair");

        auto stale = get(jobs.enqueue(request(local.resource, {Destination::Local}, Action::Retry), targets));
        local = claimed(jobs, Destination::Local, targets);
        require(get(jobs.beginAttempt(local, {"objects/stale", std::string(64, 'a'), 3})), "stale intent missing");
        mysql::MySqlResourceWriteInspector inspector(pool);
        require(inspector.inspect("song", audio, local.native_claim_generation, "objects/stale") == model::ResourceWriteDisposition::Active, "active task journal was discarded");
        execute(pool, "UPDATE resource_transfer_tasks SET claimed_at=1 WHERE task_id=" + std::to_string(local.task_id));
        require(get(jobs.recover(Destination::Local, 1)) == 1 && !get(jobs.complete(local, receipt(local, "objects/stale"))), "recovery failed to fence old worker");
        require(inspector.inspect("song", audio, local.native_claim_generation, "objects/stale") == model::ResourceWriteDisposition::Active, "unresolved journal was discarded");
        auto local_repair = get(jobs.enqueue(request(local.resource, {Destination::Local}, Action::Repair), targets));
        local = claimed(jobs, Destination::Local, targets);
        require(get(jobs.complete(local, receipt(local, "objects/stale"))), "local recovery lost known committed object");
        execute(pool, "UPDATE song_resources SET status=1,claim_generation=claim_generation+1 WHERE id=" + std::to_string(audio));
        require(inspector.inspect("song", audio, local.native_claim_generation, "objects/stale") == model::ResourceWriteDisposition::Committed, "refresh discarded last referenced journal");
        execute(pool, "UPDATE song_resources SET status=2 WHERE id=" + std::to_string(audio));
        auto refreshed = get(jobs.enqueue(request(local.resource, {}, Action::RefreshContent), targets));
        require(refreshed.tasks.size() == 3 && refreshed.tasks.front().version.content_generation == 2, "explicit refresh did not advance once");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + std::to_string(audio) + " AND confirmed_content_generation=1") == 2, "refresh erased last confirmed clouds");
        require(get(jobs.cancel(refreshed.operation_id, {})), "pending cancellation failed");
        const auto pipeline_id = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        execute(pool, "UPDATE song_resources SET status=0,local_skip_generation=0,local_skip_target_revision=0 WHERE id=" + std::to_string(pipeline_id));
        c::tests::transfer_execution::verify(pool, {model::ResourceOwner::Song, pipeline_id}, targets);
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + std::to_string(pipeline_id) + " AND cloud_provider='gdrive' AND uploaded_at>0 AND verified_at>0") == 1, "real-attempt marker did not record upload time");

        // producer 只建立一层持久关联；目标取消覆盖该端产物，晚登记不能绕过取消。
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='normal' WHERE owner_type='song_artifact'");
        const auto producer_parent = get(jobs.enqueue(request({model::ResourceOwner::Song, pipeline_id}, {Destination::Baidu, Destination::Gdrive}, Action::RefreshContent), targets));
        auto producer_baidu = claimed(jobs, Destination::Baidu, targets), producer_google = claimed(jobs, Destination::Gdrive, targets);
        const auto child_request = [&](const model::ResourceTransferJob &parent) {
            auto child = request({model::ResourceOwner::SongArtifact, artifact_id}, {parent.destination});
            child.parent_operation_id = parent.operation_id;
            child.parent_task_id = parent.task_id;
            child.parent_claim_generation = parent.claim_generation;
            child.idempotency_key = "producer:" + parent.operation_id + ":" + std::to_string(parent.resource.resource_id) + ":" + std::string(model::resourceDestinationName(parent.destination));
            return child;
        };
        auto stale_producer = child_request(producer_baidu);
        ++stale_producer.parent_claim_generation;
        require(jobs.enqueue(stale_producer, targets).error() == c::application::result::ServiceErrorType::Conflict, "stale producer claim registered child work");
        const auto baidu_child = get(jobs.enqueue(child_request(producer_baidu), targets)), google_child = get(jobs.enqueue(child_request(producer_google), targets));
        require(baidu_child.parent_operation_id == producer_parent.operation_id && baidu_child.parent_task_id == producer_baidu.task_id && get(jobs.find(producer_parent.operation_id)).child_operation_ids.size() == 2, "producer relationship was not durably discoverable");
        require(get(jobs.enqueue(child_request(producer_baidu), {})).operation_id == baidu_child.operation_id, "producer replay duplicated artifact operation");
        require(get(jobs.cancel(producer_parent.operation_id, {Destination::Baidu})), "targeted parent cancellation failed");
        require(get(jobs.find(baidu_child.operation_id)).tasks.front().state == "cancelled" && get(jobs.find(google_child.operation_id)).tasks.front().state == "pending", "targeted cancellation crossed sibling artifact lane");
        require(get(jobs.fail(producer_baidu, {"cancelled", "resource", false, false})), "producer baidu cancellation did not settle");
        require(get(jobs.cancel(producer_parent.operation_id, {})) && get(jobs.find(google_child.operation_id)).tasks.front().state == "cancelled", "parent cancellation left owned child work active");
        require(get(jobs.fail(producer_google, {"cancelled", "resource", false, false})), "producer google cancellation did not settle");
        const auto late_parent = get(jobs.enqueue(request({model::ResourceOwner::Song, pipeline_id}, {Destination::Local}, Action::Retry), targets));
        const auto late_producer = claimed(jobs, Destination::Local, targets);
        require(get(jobs.cancel(late_parent.operation_id, {})), "late producer parent did not cancel");
        const auto before_late_child = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(jobs.enqueue(child_request(late_producer), targets).error() == c::application::result::ServiceErrorType::Conflict && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == before_late_child, "cancelled parent accepted a late artifact operation");
        require(get(jobs.fail(late_producer, {"cancelled", "resource", false, false})), "late producer cancellation did not settle");

        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'lyric','' FROM song_source_refs WHERE source_content_id='jobs-content' ON DUPLICATE KEY UPDATE id=song_resources.id");
        const auto cancelled_upload = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='lyric' LIMIT 1");
        execute(pool, "UPDATE song_resources SET local_skip_generation=0,status=0 WHERE id=" + std::to_string(cancelled_upload));
        const model::ResourceIdentity adapter_identity {model::ResourceOwner::Song, cancelled_upload};
        const auto frozen_tool = get(jobs.enqueue(request(adapter_identity, {Destination::Baidu}), rclone_targets));
        require(!get(jobs.claim(Destination::Baidu, targets[1].fingerprint, targets[1].adapter)), "configuration change silently switched queued adapter");
        require(get(jobs.find(frozen_tool.operation_id)).tasks.front().result_code == "adapter_changed", "adapter change was not recorded");
        const auto unknown_tool = get(jobs.enqueue(request(adapter_identity, {Destination::Baidu}), rclone_targets));
        execute(pool, "UPDATE resource_transfer_operations SET request_snapshot=JSON_REMOVE(request_snapshot,'$.adapters') WHERE operation_id='" + unknown_tool.operation_id + "'");
        require(!get(jobs.claim(Destination::Baidu, rclone_targets[1].fingerprint, "rclone")), "old task guessed its missing adapter");
        require(get(jobs.find(unknown_tool.operation_id)).tasks.front().result_code == "adapter_unresolved", "missing adapter was not preserved as unknown");
        auto unsupported_rate = targets;
        unsupported_rate[1].policy.bytes_per_second = 1024;
        require(get(jobs.enqueue(request(adapter_identity, {Destination::Baidu}), unsupported_rate)).tasks.front().state == "unavailable", "native unsupported rate was admitted before source acquisition");
        auto late = get(jobs.enqueue(request({model::ResourceOwner::Song, cancelled_upload}, {Destination::Baidu}), targets));
        auto late_job = claimed(jobs, Destination::Baidu, targets);
        require(get(jobs.beginAttempt(late_job, {"objects/late-cancel", std::string(64, 'a'), 3})), "late cancel attempt failed");
        require(get(jobs.cancel(late.operation_id, {Destination::Baidu})) && !get(jobs.complete(late_job, receipt(late_job, "objects/late-cancel"))), "cancelled upload was confirmed");
        require(get(jobs.find(late.operation_id)).tasks.front().state == "uncertain", "published cancellation discarded reconciliation intent");
        const auto late_retry = get(jobs.enqueue(request(late_job.resource, {Destination::Baidu}, Action::Retry), targets));
        require(late_retry.tasks.front().result_code == "reconcile_required", "retry redownloaded a cancelled published object");
        const auto wrong_repair = get(jobs.enqueue(request(late_job.resource, {Destination::Baidu}, Action::Repair), rclone_targets));
        require(wrong_repair.tasks.front().state == "busy" && wrong_repair.tasks.front().busy_task_id == late_job.task_id && get(jobs.find(late.operation_id)).tasks.front().state == "uncertain", "new adapter stole unresolved old object");
        get(jobs.enqueue(request(late_job.resource, {Destination::Baidu}, Action::Repair), targets));
        const auto late_repair = claimed(jobs, Destination::Baidu, targets);
        require(get(jobs.complete(late_repair, receipt(late_repair, "objects/late-cancel"))), "explicit repair could not confirm cancelled object");
        get(jobs.enqueue(request(late_job.resource, {Destination::Local}), targets));
        const auto fenced = claimed(jobs, Destination::Local, targets);
        require(get(jobs.beginAttempt(fenced, {"objects/fenced", std::string(64, 'a'), 3})), "source fence intent failed");
        execute(pool, "UPDATE song_source_refs SET copyright_id='fixture-changed-during-transfer' WHERE source_content_id='jobs-content'");
        require(!get(jobs.complete(fenced, receipt(fenced, "objects/fenced"))), "source descriptor change accepted old bytes");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'cover','' FROM song_source_refs WHERE source_content_id='jobs-content' ON DUPLICATE KEY UPDATE id=song_resources.id");
        const auto timeout_resource = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='cover' LIMIT 1");
        execute(pool, "UPDATE song_resources SET local_skip_generation=0,status=0 WHERE id=" + std::to_string(timeout_resource));
        c::tests::transfer_timeout::verify(std::make_shared<mysql::MySqlResourceTransferJobs>(pool), {model::ResourceOwner::Song, timeout_resource}, targets);

        auto deletion = get(jobs.enqueue(request(local.resource, {Destination::Baidu}, Action::Retry), targets));
        baidu = claimed(jobs, Destination::Baidu, targets);
        require(get(jobs.beginAttempt(baidu, {"objects/deleted", std::string(64, 'a'), 3})), "delete attempt missing");
        execute(pool, "DELETE FROM song_resources WHERE id=" + std::to_string(audio));
        require(!get(jobs.complete(baidu, receipt(baidu, "objects/deleted"))), "deleted resource recreated receipt");
        require(get(jobs.find(deletion.operation_id)).tasks.front().state == "blocked", "deleted task history disappeared");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + std::to_string(audio)) == 0, "resource deletion left current cloud receipt");

        execute(pool, "CREATE TRIGGER transfer_jobs_fixture_reject BEFORE INSERT ON resource_transfer_tasks FOR EACH ROW BEGIN IF NEW.destination='gdrive' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture task rejected'; END IF; END");
        auto rollback = request(historical.resources[1]);
        rollback.idempotency_key = "atomic-register-failure";
        const auto before = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(!jobs.enqueue(rollback, targets).isSuccess(), "injected registration failure accepted");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == before, "registration left partial operation");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 0, "registration left partial slots");
        execute(pool, "DROP TRIGGER transfer_jobs_fixture_reject");
        c::tests::prepared_admission::verify(pool);
        std::cout << "resource transfer jobs: atomic registration, four-owner skip, independent claims, idempotency, cancellation, reference, target switch, repair, stale recovery, deletion and rollback passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "resource transfer jobs test failed: " << error.what() << '\n';
        return 1;
    }
}
