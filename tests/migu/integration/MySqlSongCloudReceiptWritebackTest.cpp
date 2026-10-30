#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLyricArtifactRepository.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"

#include <iostream>
#include <string>

namespace test = orchestrator::modules::crawler::tests::archive;
namespace support = orchestrator::modules::crawler::tests::migu::support;
namespace mysql = test::mysql;
namespace model = test::model;
namespace {
// 仅建立旧原生认领，不创建文件或执行任何网络请求。
model::SongResourceSuccessUpdate attempt(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, int index, std::string backend = {})
{
    model::SongResourceSuccessUpdate update;
    update.resource_id = test::mrc(pool, index);
    update.claim_generation = 1;
    update.local_path = "retained/new-" + std::to_string(index) + ".mrc";
    update.size_bytes = 3;
    update.checksum = "rclone:size:3";
    update.cloud_backend = std::move(backend);
    if (!update.cloud_backend.empty()) update.cloud_path = "remote:objects/" + std::to_string(index);
    test::execute(pool, "UPDATE song_resources SET status=1,claim_generation=1,local_path='retained/previous',size_bytes=2,checksum='md5:previous' WHERE id=" + std::to_string(update.resource_id));
    return update;
}
// SQL拒绝必须同时保留原生行，不能留下本地成功而云端报告缺失的半事务。
void unchanged(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::uint64_t id)
{
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(id) + " AND status=1 AND local_path='retained/previous' AND size_bytes=2 AND checksum='md5:previous'") == 1, "rejected legacy writeback changed local fact");
}
// 本地SHA只绑定filesystem；弱云端工具报告永不冒充远端强核验。
void localAndCloud(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlSongResourceQueue queue(pool);
    auto local = attempt(pool, 1201);
    local.checksum = std::string(64, 'a');
    test::require(queue.markSucceeded(local), "local success failed after cloud column retirement");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(local.resource_id) + " AND status=2 AND local_content_generation=content_generation AND local_target_revision=1 AND reference_sha256=REPEAT('a',64) AND reference_size_bytes=3") == 1, "real local SHA did not bind current local reference");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 0, "local-only completion created a cloud receipt");
    auto stale = local;
    stale.claim_generation = 0;
    test::require(!queue.markSucceeded(stale), "stale native claim overwrote retained local file");

    auto cloud = attempt(pool, 1202, "baidupcs");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,cloud_path,source_adapter,verification,confirmed_content_generation,confirmed_target_revision,checksum,size_bytes,uploaded_at) VALUES('song'," + std::to_string(cloud.resource_id) + ",'gdrive'," + std::to_string(cloud.resource_id) + ",'google/retained','rclone','sha256_verified',1,1,REPEAT('c',64),3,77)");
    test::require(queue.markSucceeded(cloud), "legacy Baidu report was not stored in unified receipts");
    const auto id = std::to_string(cloud.resource_id);
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + id + " AND cloud_provider='baidu' AND source_adapter='baidupcs' AND task_state='succeeded' AND verification='unverified' AND confirmed_content_generation=0 AND confirmed_target_revision=0 AND checksum='' AND JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.legacy_song_writeback.checksum'))='rclone:size:3'") == 1, "weak legacy checksum became strong cloud proof");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE resource_id=" + id + " AND cloud_provider='gdrive' AND cloud_path='google/retained' AND checksum=REPEAT('c',64) AND uploaded_at=77 AND verification='sha256_verified'") == 1, "Baidu writeback touched Google confirmation");
    test::execute(pool, "UPDATE resource_cloud_receipts SET uploaded_at=123,updated_at=123 WHERE resource_id=" + id + " AND cloud_provider='baidu'");
    test::require(queue.markSucceeded(cloud) && test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE resource_id=" + id + " AND cloud_provider='baidu' AND uploaded_at=123 AND updated_at=123") == 1, "same legacy cloud report was not idempotent");
    test::execute(pool, "UPDATE song_resources SET content_generation=content_generation+1,status=1 WHERE id=" + id);
    test::require(queue.markSucceeded(cloud) && test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE resource_id=" + id + " AND cloud_provider='baidu' AND content_generation=2 AND confirmed_content_generation=0") == 1, "new source generation reused previous generation report");
}
// rclone平台显式区分百度/Google，remote-only不制造本地或参考版本。
void rclonePlatforms(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlSongResourceQueue unknown(pool);
    auto baidu = attempt(pool, 1203, "rclone");
    baidu.remote_only = true;
    test::require(!unknown.markSucceeded(baidu), "rclone silently defaulted to Google");
    unchanged(pool, baidu.resource_id);
    mysql::MySqlSongResourceQueue::SqlPaths paths;
    paths.legacy_cloud_provider = "baidu";
    mysql::MySqlSongResourceQueue baidu_queue(pool, paths);
    test::require(baidu_queue.markSucceeded(baidu), "explicit Baidu rclone report failed");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(baidu.resource_id) + " AND local_content_generation=0 AND local_target_revision=0 AND reference_sha256='' AND reference_size_bytes=0") == 1, "remote object was declared a filesystem confirmation");
    paths.legacy_cloud_provider = "gdrive";
    mysql::MySqlSongResourceQueue google_queue(pool, paths);
    auto google = attempt(pool, 1204, "rclone");
    google.remote_only = true;
    test::require(google_queue.markSucceeded(google), "explicit Google rclone report failed");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE resource_id=" + std::to_string(google.resource_id) + " AND cloud_provider='gdrive' AND source_adapter='rclone' AND checksum='' AND confirmed_content_generation=0") == 1, "Google rclone report acquired false SHA confirmation");
}
// 历史跳过、原生/持久任务围栏与审计故障均不能被旧writer覆盖。
void protectedAndRollback(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlSongResourceQueue queue(pool);
    auto history = attempt(pool, 1205, "baidupcs");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,transfer_policy,policy_content_generation,policy_target_revision,task_state) VALUES('song'," + std::to_string(history.resource_id) + ",'baidu'," + std::to_string(history.resource_id) + ",'skip',1,1,'skipped')");
    test::require(!queue.markSucceeded(history), "legacy writer cleared historical cloud skip");
    unchanged(pool, history.resource_id);
    auto pending = attempt(pool, 1206, "baidupcs");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,task_state,attempt) VALUES('song'," + std::to_string(pending.resource_id) + ",'baidu'," + std::to_string(pending.resource_id) + ",'uncertain',JSON_OBJECT('retained',true))");
    test::require(!queue.markSucceeded(pending), "legacy writer discarded uncertain cloud intent");
    unchanged(pool, pending.resource_id);
    auto blocked = attempt(pool, 1213, "baidupcs");
    test::execute(pool, "INSERT INTO resource_baseline_blocks(owner_type,resource_id,content_generation,baseline_id,reason) VALUES('song'," + std::to_string(blocked.resource_id) + ",1,'reviewed','excluded')");
    test::require(!queue.markSucceeded(blocked), "legacy writer bypassed current baseline exclusion");
    unchanged(pool, blocked.resource_id);
    auto strong = attempt(pool, 1207, "baidupcs");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,confirmed_content_generation,confirmed_target_revision,cloud_path,verification) VALUES('song'," + std::to_string(strong.resource_id) + ",'baidu'," + std::to_string(strong.resource_id) + ",1,1,'retained/strong','sha256_verified')");
    test::require(!queue.markSucceeded(strong), "legacy report downgraded strong confirmation");
    unchanged(pool, strong.resource_id);
    const auto occupied_id = test::mrc(pool, 1208);
    test::get(mysql::MySqlResourceTransferJobs(pool).enqueue(test::request(model::ResourceOwner::Song, occupied_id, "song-cloud-occupied", {model::ResourceDestination::Gdrive}), test::profiles()));
    auto occupied = history;
    occupied.resource_id = occupied_id;
    test::execute(pool, "UPDATE song_resources SET status=1,claim_generation=1,local_path='retained/previous',size_bytes=2,checksum='md5:previous' WHERE id=" + std::to_string(occupied_id));
    test::require(!queue.markSucceeded(occupied), "legacy writer took another platform durable slot");
    unchanged(pool, occupied_id);

    auto fault = attempt(pool, 1210, "baidupcs");
    test::execute(pool, "CREATE TRIGGER fail_legacy_song_receipt BEFORE INSERT ON resource_cloud_receipts FOR EACH ROW BEGIN IF NEW.owner_type='song' AND NEW.resource_id=" + std::to_string(fault.resource_id) + " THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture'; END IF; END");
    test::require(!queue.markSucceeded(fault), "cloud receipt failure returned local success");
    unchanged(pool, fault.resource_id);
    test::execute(pool, "DROP TRIGGER fail_legacy_song_receipt");
    test::require(queue.markSucceeded(fault), "rolled back native claim could not be retried");
}
// 单云旧视图不得因第二端后来成功而隐式切换平台，也不从当前配置伪造adapter。
void projections(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlMusicCatalogQueryRepository catalog(pool);
    const auto id = test::mrc(pool, 1211);
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,cloud_path,evidence,verification) VALUES('song'," + std::to_string(id) + ",'baidu'," + std::to_string(id) + ",'declared/only',JSON_OBJECT('retired_legacy',JSON_OBJECT('cloud_uploaded_at',345)),'operator_attested')");
    auto view = catalog.findResource(id);
    test::require(view && view->cloud_backend.empty(), "unique legacy path invented backend or disappeared");
    test::require(view->cloud_path == "declared/only", "unique legacy cloud path disappeared");
    test::require(view->cloud_uploaded_at == 345, "unique legacy path lost archived upload time");
    model::MiguResourceQueueQuery query;
    query.source = "migu";
    query.page_size = 200;
    bool found = false;
    for (const auto &item : catalog.listResources(query).items)
        if (item.resource_id == id) found = item.cloud_backend.empty() && item.cloud_path == "declared/only" && item.cloud_uploaded_at == 345;
    test::require(found, "paged unique legacy path lost archived time or invented backend");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,cloud_path,source_adapter,uploaded_at,evidence) VALUES('song'," + std::to_string(id) + ",'gdrive'," + std::to_string(id) + ",'google/selected','rclone',456,JSON_OBJECT('retired_legacy',JSON_OBJECT('cloud_uploaded_at',999)))");
    view = catalog.findResource(id);
    test::require(view && view->cloud_backend.empty() && view->cloud_path.empty() && view->cloud_uploaded_at == 0, "unconfigured old view guessed one of two platforms");
    test::execute(pool, "INSERT INTO resource_legacy_profiles(profile_key,cloud_provider,created_at) VALUES('migu','gdrive',1)");
    view = catalog.findResource(id);
    test::require(view && view->cloud_backend == "rclone" && view->cloud_path == "google/selected" && view->cloud_uploaded_at == 456, "fixed old view did not select unified Google receipt");
    const auto listed = catalog.listResources(query);
    found = false;
    for (const auto &item : listed.items)
        if (item.resource_id == id) found = item.cloud_backend == "rclone" && item.cloud_path == "google/selected" && item.cloud_uploaded_at == 456;
    test::require(found, "paged old view drifted from single-resource projection");
    test::execute(pool, "UPDATE resource_legacy_profiles SET cloud_provider='local' WHERE profile_key='migu'");
    view = catalog.findResource(id);
    test::require(view && view->cloud_path.empty(), "local-only old view exposed extra cloud target");
}
// 派生身份升级保留最后云端确认，代次变化使其不再被当作新内容确认。
void artifacts(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    auto source = attempt(pool, 1212);
    source.checksum = std::string(64, 'a');
    test::require(mysql::MySqlSongResourceQueue(pool).markSucceeded(source), "artifact source local confirmation failed");
    model::LyricArtifactBundle bundle {source.resource_id, source.checksum, "legacy-1", 1, {{"word_json", "retained/word.json", 3, std::string(64, 'b')}, {"enhanced_lrc", "retained/enhanced.lrc", 4, std::string(64, 'c')}}};
    mysql::MySqlLyricArtifactRepository repository(pool);
    test::get(repository.record(bundle));
    const auto id = test::scalar(pool, "SELECT id FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(source.resource_id) + " AND artifact_type='word_json'");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,artifact_resource_id,task_state,cloud_path,checksum,verification,confirmed_content_generation,confirmed_target_revision) VALUES('song_artifact'," + std::to_string(id) + ",'baidu'," + std::to_string(id) + ",'succeeded','retained/last-cloud',REPEAT('b',64),'sha256_verified',1,1)");
    test::execute(pool, "UPDATE song_resources SET checksum=REPEAT('d',64),content_generation=content_generation+1,reference_sha256=REPEAT('d',64) WHERE id=" + std::to_string(source.resource_id));
    bundle.source_checksum = std::string(64, 'd');
    bundle.processor_version = "legacy-2";
    bundle.files.front().checksum = std::string(64, 'e');
    bundle.files.front().local_path = "retained/word-v2.json";
    test::get(repository.record(bundle));
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts a JOIN resource_cloud_receipts c ON c.owner_type='song_artifact' AND c.resource_id=a.id WHERE a.id=" + std::to_string(id) + " AND a.content_generation=2 AND a.local_path='retained/word-v2.json' AND c.cloud_path='retained/last-cloud' AND c.confirmed_content_generation=1 AND c.confirmed_content_generation<>a.content_generation") == 1, "artifact identity update lost last cloud receipt or promoted it to new content");
}
} // namespace

int main()
{
    try {
        test::require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_song_cloud_"), "refusing non-isolated song cloud database");
        const auto pool = support::makeCoverTestPool();
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM songs") == 0, "song cloud writeback test needs empty database");
        test::activate(pool);
        localAndCloud(pool);
        rclonePlatforms(pool);
        protectedAndRollback(pool);
        projections(pool);
        artifacts(pool);
        std::cout << "PASS: retired song cloud columns, atomic local/unified weak reports, explicit rclone provider, version/skip/intent/slot fences and fixed old projections; no content I/O\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
