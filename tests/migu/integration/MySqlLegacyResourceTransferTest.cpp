#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransfer.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <future>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
template <class T>
T get(c::application::result::ServiceResult<T> result)
{
    if (!result.isSuccess()) throw std::runtime_error(result.message());
    return *result.data();
}
boost::mysql::results execute(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::string sql)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "legacy projection fixture");
}
std::uint64_t scalar(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::string sql)
{
    const auto rows = execute(pool, std::move(sql));
    return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
std::vector<model::ResourceTransferTargetProfile> profiles()
{
    std::vector<model::ResourceTransferTargetProfile> out;
    for (auto d : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
        model::ResourceTransferTargetProfile p;
        p.destination = d;
        p.fingerprint = std::string(64, char('a' + static_cast<int>(d)));
        p.adapter = d == model::ResourceDestination::Local ? "filesystem" : d == model::ResourceDestination::Baidu ? "baidupcs" :
                                                                                                                     "rclone";
        out.push_back(p);
    }
    return out;
}
void finish(mysql::MySqlResourceTransferJobs &jobs, const model::ResourceTransferJob &job, std::uint8_t related_mask = 0)
{
    model::ResourceTransferReceipt receipt;
    receipt.path = "objects/legacy-" + std::to_string(job.task_id);
    receipt.checksum = std::string(64, 'a');
    receipt.size_bytes = 3;
    receipt.adapter = job.adapter;
    receipt.verification = model::ResourceVerification::Sha256Verified;
    receipt.verification_method = job.destination == model::ResourceDestination::Local ? "local_sha256" : "fixture_sha256";
    if (job.destination != model::ResourceDestination::Local) {
        receipt.remote_hash_algorithm = "sha256";
        receipt.remote_hash_value = receipt.checksum;
    }
    require(get(jobs.beginAttempt(job, {receipt.path, receipt.checksum, receipt.size_bytes, related_mask})), "legacy begin attempt failed");
    require(get(jobs.complete(job, receipt)), "legacy completion failed");
}
} // namespace
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_legacy_transfer_"), "refusing non-isolated legacy fixture");
        auto pool = support::makeCoverTestPool();
        require(scalar(pool, "SELECT COUNT(*) FROM songs") == 0, "requires empty legacy fixture");
        model::MetadataWriteBatch seed;
        for (int i = 0; i < 201; ++i) {
            model::SongSourceMetadata s;
            s.source = "migu";
            s.source_song_id = "legacy-song-" + std::to_string(i);
            s.source_content_id = "legacy-content-" + std::to_string(i);
            s.copyright_id = "fixture";
            s.source_resource_type = "2";
            s.title = "旧批次";
            s.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
            if (i == 0) s.audio_formats.push_back({"HQ", "2", "020020", "020020", 128, 200});
            seed.songs.push_back(s);
        }
        for (int i = 0; i < 2; ++i) {
            model::SingerSourceMetadata s;
            s.source = "migu";
            s.source_singer_id = "legacy-singer-" + std::to_string(i);
            s.name = "兼容歌手";
            s.avatar_url = "https://example.test/cover.png";
            seed.singers.push_back(s);
        }
        require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(seed).isSuccess(), "legacy fixture metadata failed");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64),target_fingerprint=CASE destination WHEN 'local' THEN REPEAT('a',64) WHEN 'baidu' THEN REPEAT('b',64) ELSE REPEAT('c',64) END");
        auto targets = profiles();
        mysql::MySqlLegacyResourceTransfer legacy(pool);
        mysql::MySqlResourceTransferJobs jobs(pool);
        model::LegacyResourceTransferRequest request;
        request.owner = model::ResourceOwner::Singer;
        request.cloud_provider = model::ResourceDestination::Baidu;
        request.profiles = targets;
        request.covers.concurrency = 1;
        const auto first = scalar(pool, "SELECT MIN(id) FROM singer_resources"), second = scalar(pool, "SELECT MAX(id) FROM singer_resources");
        request.covers.query.resource_ids = {first};
        auto batch = get(legacy.start(request));
        require(!batch.terminal && batch.operation_ids.size() == 1, "cover accepted as completed");
        const auto duplicate = get(legacy.start(request));
        require(duplicate.terminal && duplicate.progress.total == 0 && duplicate.operation_ids.empty(), "second legacy selection duplicated accepted pending work");
        auto local_future = std::async(std::launch::async, [&] { return get(jobs.claim(model::ResourceDestination::Local, targets[0].fingerprint, targets[0].adapter)); });
        auto cloud_future = std::async(std::launch::async, [&] { return get(jobs.claim(model::ResourceDestination::Baidu, targets[1].fingerprint, targets[1].adapter)); });
        const auto local = local_future.get(), cloud = cloud_future.get();
        require(local.has_value() != cloud.has_value(), "legacy concurrent claims exceeded batch cap");
        finish(jobs, local ? *local : *cloud);
        require(!get(legacy.find(batch.batch_id))->terminal, "cover omitted fixed cloud completion");
        const auto next_profile = targets[local ? 1 : 0];
        const auto next = get(jobs.claim(next_profile.destination, next_profile.fingerprint, next_profile.adapter));
        require(next.has_value(), "remaining fixed target not claimable");
        finish(jobs, *next);
        auto final = *get(legacy.find(batch.batch_id));
        require(final.terminal && final.covers.size() == 1 && final.covers[0].success, "fixed cover view failed");
        require(get(jobs.find(batch.operation_ids[0])).state == "running", "extra target did not remain independent");
        require(legacy.cancel(batch.batch_id).error() == c::application::result::ServiceErrorType::Conflict, "completed legacy view accepted stop");
        get(jobs.cancel(batch.operation_ids[0], {}));
        auto page = get(legacy.listCovers(request));
        require(page.items.size() == 1 && page.items[0].cloud.current && page.items[0].cloud.receipt.backend == "baidupcs", "fixed cloud query lost receipt");
        auto changed = request;
        changed.cloud_provider = model::ResourceDestination::Gdrive;
        require(legacy.start(changed).error() == c::application::result::ServiceErrorType::Conflict, "fixed old provider silently switched");
        request.covers.query.resource_ids = {second};
        auto cancel = get(legacy.start(request));
        require(get(legacy.cancel(cancel.batch_id)), "legacy cancellation failed");
        require(get(legacy.find(cancel.batch_id))->progress.state_str == "Cancelled", "legacy cancellation not durable");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 0, "legacy cancellation leaked slots");
        auto timeout = get(legacy.start(request));
        require(!timeout.terminal && !timeout.operation_ids.empty(), "explicit legacy process did not resume cancelled selection");
        execute(pool, "UPDATE resource_legacy_batches SET request_snapshot=JSON_SET(request_snapshot,'$.deadline',0) WHERE batch_id='" + timeout.batch_id + "'");
        const auto expired = *get(legacy.find(timeout.batch_id));
        require(expired.terminal && expired.timed_out && expired.progress.state_str == "Failed", "legacy batch timeout was not frozen");
        const auto timed_tasks = get(jobs.find(timeout.operation_ids[0])).tasks;
        require(std::any_of(timed_tasks.begin(), timed_tasks.end(), [](const auto &t) { return t.destination == model::ResourceDestination::Gdrive && t.state == "pending"; }), "legacy timeout cancelled extra cloud");
        get(jobs.cancel(timeout.operation_ids[0], {}));
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='skip',policy_content_generation=1,policy_target_revision=1,baseline_id='reviewed',skip_reason='legacy_both',verification='operator_attested',path_unresolved=1,cloud_path='legacy/unknown' WHERE owner_type='singer' AND resource_id=" + std::to_string(second) + " AND cloud_provider='baidu'");
        auto historical_cover = get(legacy.listCovers(request));
        require(historical_cover.items.size() == 1 && historical_cover.items[0].cloud.status == 4 && !historical_cover.items[0].cloud.current, "historical cloud declaration was projected as missing or current");
        request.covers.query.cloud_statuses = {0};
        require(get(legacy.listCovers(request)).items.empty(), "cloud status zero filter included attested history");
        request.covers.query.cloud_statuses = {4};
        require(get(legacy.listCovers(request)).items.size() == 1, "cloud status four filter disagreed with projection");
        request.covers.query.cloud_statuses.clear();
        model::LegacyResourceTransferRequest related;
        related.cloud_provider = model::ResourceDestination::Baidu;
        related.profiles = {targets[0]};
        related.songs.options.process_related_resources = true;
        const auto pair = execute(pool, "SELECT r.id FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_content_id='legacy-content-0' AND r.resource_kind='audio' ORDER BY r.id");
        for (auto row : pair.rows()) related.songs.resource_ids.push_back(mysql::MySqlValueReader::uint64At(row, 0));
        const auto related_batch = get(legacy.start(related));
        const auto a = get(jobs.claim(model::ResourceDestination::Local, targets[0].fingerprint, targets[0].adapter));
        const auto b = get(jobs.claim(model::ResourceDestination::Local, targets[0].fingerprint, targets[0].adapter));
        require(a && b, "same-source qualities were not claimed");
        finish(jobs, *a, 1);
        finish(jobs, *b, 1);
        require(!get(legacy.find(related_batch.batch_id))->terminal, "busy duplicate hid owned related work");
        const auto child = get(jobs.claim(model::ResourceDestination::Local, targets[0].fingerprint, targets[0].adapter));
        require(child.has_value(), "owned related child missing");
        finish(jobs, *child);
        const auto related_done = *get(legacy.find(related_batch.batch_id));
        require(related_done.terminal && related_done.songs.succeeded_count == 2 && related_done.songs.related_succeeded_count == 1 && related_done.songs.items.size() == 3, "related duplicate inflated legacy counts");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,status,created_at,updated_at) SELECT MIN(id),'mrc',0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP() FROM song_source_refs");
        const auto mrc = scalar(pool, "SELECT MAX(id) FROM song_resources WHERE resource_kind='mrc'");
        auto post = related;
        post.songs.resource_ids = {mrc};
        post.profiles[0].produces_artifacts = true;
        post.profiles[0].producer_key_available = true;
        post.profiles[0].producer_version = "fixture_v1";
        post.profiles[0].producer_policy = {1000, 0, 1024 * 1024, 0};
        const auto post_batch = get(legacy.start(post));
        const auto mrc_job = get(jobs.claim(model::ResourceDestination::Local, targets[0].fingerprint, targets[0].adapter));
        require(mrc_job && mrc_job->resource.resource_id == mrc, "postprocessing fixture MRC not claimed");
        finish(jobs, *mrc_job);
        require(!get(legacy.find(post_batch.batch_id))->terminal, "legacy view omitted originally enabled producer");
        execute(pool, "UPDATE resource_legacy_batches SET request_snapshot=JSON_SET(request_snapshot,'$.deadline',0) WHERE batch_id='" + post_batch.batch_id + "'");
        const auto post_done = *get(legacy.find(post_batch.batch_id));
        require(post_done.terminal && !post_done.timed_out && post_done.songs.succeeded_count == 1, "postprocessing timeout changed confirmed MRC outcome");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_producer_tasks WHERE state='cancelled'") == 1, "postprocessing timeout left required producer active");
        execute(pool, "UPDATE song_resources SET status=2,local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='reviewed',local_skip_reason='both_saved'");
        const auto ids = execute(pool, "SELECT id FROM song_resources WHERE resource_kind='audio' ORDER BY id");
        model::LegacyResourceTransferRequest songs;
        songs.cloud_provider = model::ResourceDestination::Baidu;
        songs.profiles = {targets[0]};
        songs.songs.limit = 1000;
        songs.songs.options.process_related_resources = false;
        songs.songs.options.auto_refresh_pacmtoken = false;
        songs.songs.options.pacmtoken_refresh_interval_seconds = 0;
        for (auto row : ids.rows()) songs.songs.resource_ids.push_back(mysql::MySqlValueReader::uint64At(row, 0));
        songs.songs.batch_id = "OldClient-" + std::string(118, 'x');
        auto historical = get(legacy.start(songs));
        require(historical.batch_id == songs.songs.batch_id, "custom legacy batch id was replaced");
        const auto count_before_duplicate = scalar(pool, "SELECT COUNT(*) FROM crawl_batches");
        require(legacy.start(songs).error() == c::application::result::ServiceErrorType::Conflict && scalar(pool, "SELECT COUNT(*) FROM crawl_batches") == count_before_duplicate, "duplicate custom batch overwrote persistent history");
        auto invalid = songs;
        invalid.songs.batch_id += 'x';
        require(legacy.start(invalid).error() == c::application::result::ServiceErrorType::InvalidParam, "oversized custom batch id was accepted");
        require(historical.terminal && historical.operation_ids.size() == 2 && historical.songs.claimed_count == 202 && historical.songs.skipped_count == 202 && historical.songs.succeeded_count == 0, "old 1000 limit or history skip changed counts");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 0, "historical skip reserved content work");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations WHERE JSON_EXTRACT(request_snapshot,'$.source_options.auto_refresh_pacmtoken')=FALSE") == 2, "source overrides were not frozen");
        auto restarted = mysql::MySqlLegacyResourceTransfer(pool);
        require(get(restarted.find(historical.batch_id))->songs.skipped_count == 202, "restart lost immutable old result");
        const auto before = scalar(pool, "SELECT COUNT(*) FROM crawl_batches");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance' WHERE destination='baidu'");
        require(!legacy.start(songs).isSuccess() && scalar(pool, "SELECT COUNT(*) FROM crawl_batches") == before, "maintenance created legacy batch");
        std::cout << "legacy projection: atomic chunks, counts, fixed cloud, independent extra target, concurrency, cancellation, restart and maintenance passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "legacy projection test failed: " << error.what() << '\n';
        return 1;
    }
}
