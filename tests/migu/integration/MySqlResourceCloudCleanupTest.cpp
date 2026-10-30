#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanup.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"
#include <algorithm>
#include <iostream>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
namespace {
// 只对隔离库设置伪造已确认的对象证据。
void receipt(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, model::ResourceIdentity id, std::string path, std::string verification = "sha256_verified")
{
    const auto owner = std::string(model::resourceOwnerName(id.owner));
    const auto fk = id.owner == model::ResourceOwner::Song ? "song_resource_id" : id.owner == model::ResourceOwner::Singer ? "singer_resource_id" :
                                                                              id.owner == model::ResourceOwner::Album      ? "album_resource_id" :
                                                                                                                             "artifact_resource_id";
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + std::string(fk) + ",task_state,confirmed_content_generation,confirmed_target_revision,cloud_path,size_bytes,checksum,verification,source_adapter) VALUES('" + owner + "'," + std::to_string(id.resource_id) + ",'baidu'," + std::to_string(id.resource_id) + ",'succeeded',1,1,'" + path + "',3,REPEAT('a',64),'" + verification + "','rclone') ON DUPLICATE KEY UPDATE task_state='succeeded',confirmed_content_generation=1,confirmed_target_revision=1,cloud_path=VALUES(cloud_path),size_bytes=3,checksum=REPEAT('a',64),verification=VALUES(verification)");
}
// 查找固定候选，不打印对象路径。
model::ResourceCloudCleanupCandidate find(mysql::MySqlResourceCloudCleanup &cleanup, std::uint64_t id)
{
    const auto page = test::get(cleanup.preview({}, 0, 200));
    const auto found = std::find_if(page.items.begin(), page.items.end(), [id](const auto &c) { return c.id == id; });
    test::require(found != page.items.end(), "cleanup candidate missing");
    return *found;
}
} // namespace
int main()
{
    using namespace test;
    try {
        require(c::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_cleanup_"), "refusing non-isolated cleanup database");
        auto pool = c::tests::migu::support::makeCoverTestPool();
        activate(pool);
        std::vector<std::uint64_t> singers;
        for (int i = 0; i < 6; ++i) singers.push_back(singer(pool, 100 + i));
        const auto song = mrc(pool, 110);
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version) VALUES(" + std::to_string(song) + ",'word_json',REPEAT('a',64),'fixture',1)");
        const auto artifact = scalar(pool, "SELECT MAX(id) FROM song_resource_artifacts");
        model::AlbumSourceMetadata album;
        album.source_album_id = "cleanup-album";
        album.title = "fixture";
        album.cover_url = "https://example.test/cover.png";
        model::MetadataWriteBatch batch;
        batch.albums = {album};
        get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
        const auto album_id = scalar(pool, "SELECT MAX(id) FROM album_resources");
        mysql::MySqlResourceTransferJobs jobs(pool);
        get(jobs.enqueue(request(model::ResourceOwner::Singer, singers[4], "cleanup-active", {model::ResourceDestination::Baidu}), profiles()));
        const model::ResourceIdentity one {model::ResourceOwner::Singer, singers[0]}, two {model::ResourceOwner::Singer, singers[1]}, three {model::ResourceOwner::Singer, singers[2]}, four {model::ResourceOwner::Singer, singers[3]}, five {model::ResourceOwner::Singer, singers[4]}, six {model::ResourceOwner::Singer, singers[5]};
        receipt(pool, one, "old:root/resources/v1/shared/object");
        receipt(pool, two, "renamed:root/resources/v1/shared/object");
        receipt(pool, four, "https://example.test/private?token=fixture");
        receipt(pool, five, "archive:root/resources/v1/singer/active");
        receipt(pool, six, "archive:root/resources/v1/album/alias");
        mysql::MySqlResourceCloudCleanup cleanup(pool);
        require(!cleanup.capture({one}, false).isSuccess() && !cleanup.capture({one}, true).isSuccess(), "capture bypassed stopped writers or maintenance");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance'");
        execute(pool, "CREATE TRIGGER cleanup_audit_fail BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture audit failure'");
        require(!cleanup.capture({one}, true).isSuccess() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == 0, "audit failure left orphan snapshot");
        execute(pool, "DROP TRIGGER cleanup_audit_fail");
        const auto first = get(cleanup.capture({one}, true));
        require(first.inserted == 1 && first.candidate_ids.size() == 1, "first capture failed");
        const auto replay = get(cleanup.capture({one}, true));
        require(replay.inserted == 0 && replay.candidate_ids == first.candidate_ids && scalar(pool, "SELECT COUNT(*) FROM governance_events WHERE action='resource.cloud.cleanup.capture'") == 1, "capture replay duplicated evidence or audit");
        const auto first_id = first.candidate_ids.front();
        require(find(cleanup, first_id).result_code == "source_retained", "live resource suggested cleanup");
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(one.resource_id));
        require(find(cleanup, first_id).result_code == "shared_reference", "remote alias hid shared reference");
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(two.resource_id));
        require(find(cleanup, first_id).result_code == "candidate_only", "deleted unshared resource cannot reach review candidate");
        const auto lost = get(cleanup.capture({two, three}, true));
        require(lost.resources_missing == 1 && lost.resources_without_receipts == 1 && lost.candidate_ids.empty(), "missing evidence reported as no remote object");
        execute(pool, "UPDATE resource_destination_states SET target_revision=2 WHERE destination='baidu'");
        require(find(cleanup, first_id).result_code == "target_changed", "changed target did not fence cleanup");
        execute(pool, "UPDATE resource_destination_states SET target_revision=1 WHERE destination='baidu'");
        receipt(pool, three, "archive:root/resources/v1/singer/weak", "operator_attested");
        const auto weak = get(cleanup.capture({three}, true)).candidate_ids.front();
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(three.resource_id));
        require(find(cleanup, weak).result_code == "unverified_evidence", "historical declaration became deletion proof");
        const auto unsafe = get(cleanup.capture({four}, true)).candidate_ids.front();
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates WHERE cloud_path LIKE '%://%'") == 0, "unsafe URL was copied into cleanup evidence");
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(four.resource_id));
        require(find(cleanup, unsafe).cloud_path.empty() && find(cleanup, unsafe).result_code == "unsafe_or_unresolved_path", "unsafe stored URL was exposed");
        const auto active = get(cleanup.capture({five}, true)).candidate_ids.front();
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(five.resource_id));
        require(find(cleanup, active).result_code == "active_or_uncertain_intent", "active durable task lost protection after source deletion");
        const auto alias = get(cleanup.capture({six}, true)).candidate_ids.front();
        execute(pool, "INSERT INTO resource_cloud_migration_audit(run_id,owner_type,resource_id,content_generation,source_identity,old_values,disposition,created_at) VALUES('cleanup-alias','album'," + std::to_string(six.resource_id) + ",1,REPEAT('a',64),JSON_OBJECT('path','resources/v1/album/alias'),'alias_retired',UNIX_TIMESTAMP())");
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(six.resource_id));
        require(find(cleanup, alias).result_code == "alias_evidence_requires_review", "alias archive failed to protect object");
        const std::vector<model::ResourceIdentity> extra = {{model::ResourceOwner::Song, song}, {model::ResourceOwner::Album, album_id}, {model::ResourceOwner::SongArtifact, artifact}};
        for (const auto id : extra) receipt(pool, id, "archive:root/resources/v1/" + std::string(model::resourceOwnerName(id.owner)) + "/fixture");
        const auto captured = get(cleanup.capture(extra, true));
        require(captured.candidate_ids.size() == 3, "unified cleanup owner scope incomplete");
        // 显式capture后父级联删除不会丢失证据；不依赖FK触发子表DELETE触发器。
        execute(pool, "DELETE FROM song_resources WHERE id=" + std::to_string(song));
        execute(pool, "DELETE FROM album_resources WHERE id=" + std::to_string(album_id));
        for (const auto id : captured.candidate_ids) require(find(cleanup, id).result_code == "candidate_only", "captured cascade evidence was lost");
        const model::ResourceIdentity native_intent {model::ResourceOwner::Singer, singer(pool, 120)};
        receipt(pool, native_intent, "archive:root/resources/v1/singer/native-intent");
        execute(pool, "UPDATE resource_cloud_receipts SET attempt=JSON_OBJECT('path','resources/v1/native/pending') WHERE owner_type='singer' AND resource_id=" + std::to_string(native_intent.resource_id));
        const auto uncertain = get(cleanup.capture({native_intent}, true)).candidate_ids.front();
        execute(pool, "DELETE FROM singer_resources WHERE id=" + std::to_string(native_intent.resource_id));
        require(find(cleanup, uncertain).result_code == "active_or_uncertain_intent", "native attempt evidence disappeared with receipt cascade");
        const auto page = get(cleanup.preview(model::ResourceDestination::Baidu, 0, 1));
        require(page.items.size() == 1 && page.may_have_more && get(cleanup.preview({}, page.next_after_id, 200)).items.size() + 1 == get(cleanup.preview({}, 0, 200)).items.size(), "cleanup cursor pagination changed scope");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1 WHERE destination='baidu'");
        require(find(cleanup, first_id).result_code == "maintenance_required", "active writer preview suggested cleanup");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == 1, "read-only preview created transfer operations");
        std::cout << "PASS: cleanup capture/replay/audit rollback, cascades, references, target, intent, aliases, redaction and four owners; no remote deletes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
