#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumRepository.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanup.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongRepository.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
namespace support = c::tests::migu::support;
namespace {
// Fixture只写固定无凭据对象，不进行真实上传。
void receipts(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, model::ResourceIdentity id)
{
    const auto owner = std::string(model::resourceOwnerName(id.owner));
    const auto fk = id.owner == model::ResourceOwner::Song ? "song_resource_id" : id.owner == model::ResourceOwner::Singer ? "singer_resource_id" :
                                                                              id.owner == model::ResourceOwner::Album      ? "album_resource_id" :
                                                                                                                             "artifact_resource_id";
    for (const auto *provider : {"baidu", "gdrive"})
        test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + std::string(fk) + ",task_state,confirmed_content_generation,confirmed_target_revision,cloud_path,size_bytes,checksum,verification,source_adapter) VALUES('" + owner + "'," + std::to_string(id.resource_id) + ",'" + provider + "'," + std::to_string(id.resource_id) + ",'succeeded',1,1,'archive:root/resources/v1/" + owner + "/" + std::to_string(id.resource_id) + "',3,REPEAT('a',64),'sha256_verified','rclone')");
}

// 仓储与ID在异步等待结束前保持存活。
template <typename Repository, typename Id>
bool remove(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, Repository &repository, Id id)
{
    return support::runOnExecutor(pool->executor(), repository.coDeleteById(id), "parent deletion fixture");
}

// 查询父实体，与资源ID区分。
std::uint64_t songParent(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::uint64_t resource)
{
    return test::scalar(pool, "SELECT s.song_db_id FROM song_source_refs s JOIN song_resources r ON r.song_source_ref_id=s.id WHERE r.id=" + std::to_string(resource));
}

// 查询歌手名字池父实体。
std::int64_t singerParent(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::uint64_t resource)
{
    return static_cast<std::int64_t>(test::scalar(pool, "SELECT s.singer_db_id FROM singer_source_refs s JOIN singer_resources r ON r.singer_source_ref_id=s.id WHERE r.id=" + std::to_string(resource)));
}

// 父歌曲删除须覆盖原MRC、artifact双端及旧弱证据。
void songCascade(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    const auto song = mrc(pool, 401);
    const auto parent = songParent(pool, song);
    execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version) VALUES(" + std::to_string(song) + ",'word_json',REPEAT('a',64),'fixture',1)");
    const auto artifact = scalar(pool, "SELECT MAX(id) FROM song_resource_artifacts");
    receipts(pool, {model::ResourceOwner::Song, song});
    receipts(pool, {model::ResourceOwner::SongArtifact, artifact});
    execute(pool, "UPDATE song_resources SET checksum='rclone:size:3',size_bytes=3 WHERE id=" + std::to_string(song));
    c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Song, song}, {{"cloud_backend", "rclone"}, {"cloud_path", "old:retained/object"}});
    execute(pool, "UPDATE song_resource_artifacts SET checksum='rclone:size:5',size_bytes=5 WHERE id=" + std::to_string(artifact));
    c::tests::retired::cloudArchive(pool, {model::ResourceOwner::SongArtifact, artifact}, {{"cloud_backend", "rclone"}, {"cloud_path", "old:retained/artifact"}});
    const auto before = scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates");
    mysql::MySqlSongRepository repository(pool, {});
    require(remove(pool, repository, parent), "song parent delete failed");
    require(scalar(pool, "SELECT COUNT(*) FROM songs WHERE id=" + std::to_string(parent)) == 0 && scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE id=" + std::to_string(artifact)) == 0, "song cascade incomplete");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == before + 4, "song/artifact platform evidence lost in cascade");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE disposition='resource_deleted' AND owner_type='song' AND resource_id=" + std::to_string(song) + " AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.checksum'))='rclone:size:3' AND JSON_EXTRACT(old_values,'$.cloud_size_bytes')=3 AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.verification'))='legacy_unverified'") == 1, "legacy weak cloud evidence was lost or strengthened");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE disposition='resource_deleted' AND owner_type='song_artifact' AND resource_id=" + std::to_string(artifact) + " AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.cloud_checksum'))='rclone:size:5' AND JSON_EXTRACT(old_values,'$.cloud_size_bytes')=5") == 1, "artifact legacy cloud size was lost");
    const auto audits = scalar(pool, "SELECT COUNT(*) FROM governance_events WHERE action='resource.cloud.cleanup.capture'");
    require(!remove(pool, repository, parent) && scalar(pool, "SELECT COUNT(*) FROM governance_events WHERE action='resource.cloud.cleanup.capture'") == audits, "missing parent replay changed capture audit");
}

// 真实父仓储删除失败时，证据与原资源共同回滚。
void rollbackAndSingerSources(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    const auto one = singer(pool, 402);
    const auto parent = singerParent(pool, one);
    execute(pool, "INSERT INTO singer_source_refs(singer_db_id,source,source_singer_id) VALUES(" + std::to_string(parent) + ",'qq','delete-second-source')");
    execute(pool, "INSERT INTO singer_resources(singer_source_ref_id) SELECT id FROM singer_source_refs WHERE source='qq' AND source_singer_id='delete-second-source'");
    const auto two = scalar(pool, "SELECT MAX(id) FROM singer_resources");
    receipts(pool, {model::ResourceOwner::Singer, one});
    receipts(pool, {model::ResourceOwner::Singer, two});
    c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, one}, {{"cloud_backend", "baidupcs"}, {"cloud_path", "old:retained/singer"}, {"cloud_checksum", "rclone:size:123456789012"}, {"cloud_size_bytes", 123456789012ULL}});
    const auto before = scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates");
    mysql::MySqlSingerRepository repository(pool, {});
    execute(pool, "CREATE TRIGGER deletion_audit_fail BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture audit failure'");
    require(!remove(pool, repository, parent), "audit failure deleted singer");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == before && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='singer' AND resource_id IN(" + std::to_string(one) + "," + std::to_string(two) + ")") == 4, "audit failure left partial deletion/capture");
    execute(pool, "DROP TRIGGER deletion_audit_fail");
    execute(pool, "CREATE TRIGGER deletion_parent_fail BEFORE DELETE ON singers FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture deletion failure'");
    require(!remove(pool, repository, parent) && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == before, "parent delete failure committed evidence separately");
    execute(pool, "DROP TRIGGER deletion_parent_fail");
    require(remove(pool, repository, parent), "multi-source singer delete failed");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == before + 4 && scalar(pool, "SELECT COUNT(*) FROM singer_source_refs WHERE singer_db_id=" + std::to_string(parent)) == 0, "parent delete omitted another platform source");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE disposition='resource_deleted' AND owner_type='singer' AND resource_id=" + std::to_string(one) + " AND JSON_EXTRACT(old_values,'$.cloud_size_bytes')=123456789012 AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.cloud_checksum'))='rclone:size:123456789012'") == 1, "singer legacy cloud size was lost or truncated");
}

// 未决及原生旧云端意图禁止删除，不能用父级联绕过。
void busyFences(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    const auto resource = singer(pool, 403);
    const auto parent = singerParent(pool, resource);
    receipts(pool, {model::ResourceOwner::Singer, resource});
    mysql::MySqlSingerRepository repository(pool, {});
    const auto before = scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates");
    for (const auto *change : {"status=1", "status=0,running_batch_id='fixture-claim'"}) {
        execute(pool, "UPDATE singer_resources SET " + std::string(change) + " WHERE id=" + std::to_string(resource));
        require(!remove(pool, repository, parent), "native claim fence bypassed");
    }
    execute(pool, "UPDATE singer_resources SET running_batch_id='' WHERE id=" + std::to_string(resource));
    execute(pool, "UPDATE resource_cloud_receipts SET task_state='uncertain' WHERE owner_type='singer' AND resource_id=" + std::to_string(resource) + " AND cloud_provider='baidu'");
    require(!remove(pool, repository, parent), "unified cloud uncertainty fence bypassed");
    execute(pool, "UPDATE resource_cloud_receipts SET task_state='succeeded' WHERE owner_type='singer' AND resource_id=" + std::to_string(resource));
    execute(pool, "UPDATE resource_cloud_receipts SET attempt=JSON_OBJECT('path','pending') WHERE owner_type='singer' AND resource_id=" + std::to_string(resource) + " AND cloud_provider='baidu'");
    require(!remove(pool, repository, parent), "receipt uncertain intent was deleted");
    execute(pool, "UPDATE resource_cloud_receipts SET attempt=NULL WHERE owner_type='singer' AND resource_id=" + std::to_string(resource));
    mysql::MySqlResourceTransferJobs jobs(pool);
    execute(pool, "UPDATE singer_resources SET local_path='',checksum='',size_bytes=0 WHERE id=" + std::to_string(resource));
    const auto operation = get(jobs.enqueue(request(model::ResourceOwner::Singer, resource, "delete-busy"), profiles()));
    require(!remove(pool, repository, parent) && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates") == before, "durable slot allowed deletion or leaked evidence");
    get(jobs.cancel(operation.operation_id, {}));
    require(remove(pool, repository, parent), "cancelled resource remained permanently undeletable");
}

// 父传输已终态但producer活跃，或artifact被占用，均不得由祖先删除绕过。
void producerAndArtifactFences(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    const auto source = mrc(pool, 405);
    const auto parent = songParent(pool, source);
    mysql::MySqlResourceTransferJobs jobs(pool);
    const std::vector<model::ResourceDestination> destinations {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive};
    const auto operation = get(jobs.enqueue(request(model::ResourceOwner::Song, source, "delete-producer", destinations), profiles(true)));
    execute(pool, "UPDATE resource_transfer_tasks SET state='succeeded' WHERE operation_id='" + operation.operation_id + "'");
    execute(pool, "DELETE s FROM resource_transfer_slots s JOIN resource_transfer_tasks t ON t.task_id=s.task_id WHERE t.operation_id='" + operation.operation_id + "'");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_producer_tasks WHERE parent_operation_id='" + operation.operation_id + "' AND state='pending'") == 3, "producer guard fixture absent");
    mysql::MySqlSongRepository repository(pool, {});
    for (const auto destination : destinations) {
        execute(pool, "UPDATE resource_producer_tasks SET state='cancelled' WHERE parent_operation_id='" + operation.operation_id + "'");
        execute(pool, "UPDATE resource_producer_tasks SET state='pending' WHERE parent_operation_id='" + operation.operation_id + "' AND destination='" + std::string(model::resourceDestinationName(destination)) + "'");
        require(!remove(pool, repository, parent), "terminal parent task hid active producer on one destination");
    }
    execute(pool, "UPDATE resource_producer_tasks SET state='cancelled' WHERE parent_operation_id='" + operation.operation_id + "'");
    execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version) VALUES(" + std::to_string(source) + ",'word_json',REPEAT('a',64),'fixture',1)");
    const auto artifact = scalar(pool, "SELECT MAX(id) FROM song_resource_artifacts");
    const auto child = get(jobs.enqueue(request(model::ResourceOwner::SongArtifact, artifact, "delete-artifact", destinations), profiles()));
    require(child.tasks.size() == 3, "artifact waiting-input scope incomplete");
    for (const auto &task : child.tasks) require(task.state == "waiting_input", "artifact waiting-input fixture absent");
    require(!remove(pool, repository, parent), "artifact waiting-input intent lost under song cascade");
    for (const auto destination : destinations) {
        get(jobs.cancel(child.operation_id, {destination}));
        if (destination != model::ResourceDestination::Gdrive) require(!remove(pool, repository, parent), "sibling waiting-input intent was deleted");
    }
    for (const auto &task : get(jobs.find(child.operation_id)).tasks) require(task.state == "cancelled", "waiting-input file task ignored cancellation");
    require(remove(pool, repository, parent), "reconciled artifact prevented parent deletion");
}

// 音频占位清理经metadata公开事务触发，保留两个平台事实。
void placeholder(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    const auto source_resource = mrc(pool, 404);
    const auto source = scalar(pool, "SELECT song_source_ref_id FROM song_resources WHERE id=" + std::to_string(source_resource));
    const auto missing = scalar(pool, "SELECT id FROM song_resources WHERE song_source_ref_id=" + std::to_string(source) + " AND resource_kind='audio' AND format_type=''");
    receipts(pool, {model::ResourceOwner::Song, missing});
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "archive-404";
    song.source_content_id = song.source_song_id;
    song.title = song.source_song_id;
    model::SongSourceAudioFormat format;
    format.format_type = "PQ";
    song.audio_formats = {format};
    model::MetadataWriteBatch batch;
    batch.songs = {song};
    get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
    require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(missing)) == 0, "obsolete audio placeholder remains");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates WHERE owner_type='song' AND resource_id=" + std::to_string(missing)) == 2, "placeholder cleanup lost platform receipts");
}

// 别名合并自动捕获，普通父专辑删除同样捕获，均不需全局停写。
void albumAndAlias(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    model::AlbumSourceMetadata album;
    album.source_album_id = "delete-alias";
    album.title = "fixture-delete-alias";
    album.source_album_kind = 1;
    album.source_resource_type = "2003";
    album.cover_url = "https://example.test/cover.png";
    model::MetadataWriteBatch batch;
    batch.albums = {album};
    mysql::MySqlMetadataBatchWriter writer(pool);
    get(writer.saveBatch(batch));
    const auto old = scalar(pool, "SELECT MAX(id) FROM album_resources");
    receipts(pool, {model::ResourceOwner::Album, old});
    c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Album, old}, {{"cloud_backend", "rclone"}, {"cloud_path", "old:retained/alias"}, {"cloud_size_bytes", 4294967297ULL}});
    auto canonical = album;
    canonical.source_album_id = "delete-canonical";
    canonical.source_material_id = album.source_album_id;
    canonical.source_album_kind = 2;
    canonical.source_resource_type = "5";
    canonical.detail_sync_status = 2;
    batch.albums = {canonical};
    get(writer.saveBatch(batch));
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates WHERE owner_type='album' AND resource_id=" + std::to_string(old)) == 2, "alias deletion did not auto-capture");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE disposition='resource_deleted' AND owner_type='album' AND resource_id=" + std::to_string(old) + " AND JSON_EXTRACT(old_values,'$.cloud_size_bytes')=4294967297") == 1, "album alias legacy cloud size was lost or truncated");
    const auto current = scalar(pool, "SELECT r.id FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id WHERE s.source_album_id='delete-canonical'");
    receipts(pool, {model::ResourceOwner::Album, current});
    c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Album, current}, {{"cloud_backend", "baidupcs"}, {"cloud_path", "old:retained/album"}, {"cloud_size_bytes", 9876543210ULL}});
    const auto parent = scalar(pool, "SELECT s.album_db_id FROM album_source_refs s JOIN album_resources r ON r.album_source_ref_id=s.id WHERE r.id=" + std::to_string(current));
    mysql::MySqlAlbumRepository repository(pool, {});
    require(remove(pool, repository, parent), "album parent deletion failed");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_cleanup_candidates WHERE owner_type='album' AND resource_id=" + std::to_string(current)) == 2, "album parent evidence missing");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE disposition='resource_deleted' AND owner_type='album' AND resource_id=" + std::to_string(current) + " AND JSON_EXTRACT(old_values,'$.cloud_size_bytes')=9876543210") == 1, "album parent legacy cloud size was lost or truncated");
}
} // namespace

int main()
{
    try {
        test::require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_deletion_"), "refusing non-isolated deletion database");
        const auto pool = support::makeCoverTestPool();
        test::activate(pool);
        songCascade(pool);
        rollbackAndSingerSources(pool);
        busyFences(pool);
        producerAndArtifactFences(pool);
        placeholder(pool);
        albumAndAlias(pool);
        std::cout << "PASS: application parent/placeholder/alias deletion automatically captures four-owner dual-platform receipts, preserves legacy evidence and rolls back on busy/audit/delete failures; no remote deletes\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
