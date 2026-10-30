#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlProducedArtifactRegistry.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <cstdint>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace {
// 测试诊断不回显数据库连接或对象路径。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 只用于隔离 fixture。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "lifecycle fixture");
}
// 读取固定测试计数。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    auto rows = execute(pool, std::move(sql));
    return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 返回实际服务数据，失败时保留固定脱敏消息。
template <class T>
T get(c::application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 创建明确的 Migu 物料来源，不猜同名专辑。
model::AlbumSourceMetadata album(const std::string &id)
{
    model::AlbumSourceMetadata value;
    value.source_album_id = id;
    value.title = id;
    value.source_album_kind = 1;
    value.source_resource_type = "2003";
    value.cover_url = "https://example.test/album.png";
    return value;
}
// 通过公开writer触发真实的别名业务事务。
bool save(mysql::MySqlMetadataBatchWriter &writer, model::AlbumSourceMetadata value)
{
    model::MetadataWriteBatch batch;
    batch.albums = {std::move(value)};
    return writer.saveBatch(batch).isSuccess();
}
// 完整参考仅为测试构造的持久证据，不声称真实云端核验。
void verified(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::uint64_t resource, char hash)
{
    execute(pool, "UPDATE album_resources SET status=2,local_path='fixture/retained.png',checksum=REPEAT('" + std::string(1, hash) + "',64),size_bytes=3,reference_sha256=REPEAT('" + std::string(1, hash) + "',64),reference_size_bytes=3,local_content_generation=content_generation,local_target_revision=1 WHERE id=" + std::to_string(resource));
}
// 按明确来源标识查fixture资源。
std::uint64_t resource(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &source)
{
    return scalar(pool, "SELECT r.id FROM album_resources r JOIN album_source_refs s ON s.id=r.album_source_ref_id WHERE s.source_album_id='" + source + "'");
}

// 实际生产注册事务只保存引用，不能因为云端生成而制造正式 local 成功。
void producer(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    model::MetadataWriteBatch batch;
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "producer-song";
    song.source_content_id = "producer-content";
    song.title = "派生产物事务";
    batch.songs = {song};
    require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch).isSuccess(), "producer metadata fixture failed");
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'mrc','' FROM song_source_refs WHERE source_song_id='producer-song' ON DUPLICATE KEY UPDATE id=song_resources.id");
    const auto source = scalar(pool, "SELECT r.id FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_song_id='producer-song' AND r.resource_kind='mrc'");
    execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64)");
    std::vector<model::ResourceTransferTargetProfile> profiles;
    for (const auto destination : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
        model::ResourceTransferTargetProfile profile;
        profile.destination = destination;
        profile.fingerprint = std::string(64, destination == model::ResourceDestination::Baidu ? 'd' : 'c');
        profile.adapter = "rclone";
        profile.produces_artifacts = true;
        profile.producer_version = "decoder-1-encoder-1";
        profile.producer_policy.max_bytes = 1024 * 1024;
        profile.producer_key_available = true;
        profiles.push_back(profile);
    }
    mysql::MySqlResourceTransferJobs jobs(pool);
    model::ResourceTransferRequest request;
    request.resources = {{model::ResourceOwner::Song, source}};
    request.destinations = {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive};
    const auto operation = get(jobs.enqueue(request, profiles));
    const auto first = get(jobs.claim(model::ResourceDestination::Baidu, std::string(64, 'd'), "rclone"));
    const auto second = get(jobs.claim(model::ResourceDestination::Gdrive, std::string(64, 'c'), "rclone"));
    require(first && second, "producer parents not independently claimable");
    model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::seconds(30), {}};
    model::LyricArtifactBundle bundle {source, std::string(64, 'f'), "decoder-1-encoder-1", 1, {{"word_json", {}, 3, std::string(64, 'a')}, {"enhanced_lrc", {}, 4, std::string(64, 'b')}}};
    mysql::MySqlProducedArtifactRegistry registry(pool);
    const auto identities = get(registry.record(*first, bundle, 9, context));
    require(identities.size() == 2 && get(registry.record(*second, bundle, 9, context)) == identities, "same output was not shared across independent targets");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(source) + " AND COALESCE(local_path,'')='' AND checksum='' AND size_bytes=0 AND status=0 AND LENGTH(reference_sha256)=64") == 2, "producer fabricated local receipts");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song_artifact'") == 0, "producer fabricated cloud receipts");
    auto conflict = bundle;
    conflict.source_checksum = std::string(64, 'e');
    require(!registry.record(*second, conflict, 9, context).isSuccess(), "different MRC bytes replaced shared source reference");
    conflict = bundle;
    conflict.files.back().checksum = std::string(64, 'e');
    require(!registry.record(*second, conflict, 9, context).isSuccess(), "same producer identity accepted different output");
    const auto retained = std::to_string(identities.front().resource_id);
    execute(pool, "UPDATE song_resource_artifacts SET status=2,local_path='fixture/previous-artifact',checksum=reference_sha256,size_bytes=reference_size_bytes,local_content_generation=content_generation,local_target_revision=1 WHERE id=" + retained);
    execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,artifact_resource_id,cloud_path,checksum,confirmed_content_generation,confirmed_target_revision) SELECT 'song_artifact',id,'baidu',id,'fixture/previous-cloud',reference_sha256,content_generation,2 FROM song_resource_artifacts WHERE id=" + retained);
    model::ResourceTransferRequest child;
    child.resources = {identities.back()};
    child.destinations = {model::ResourceDestination::Gdrive};
    const auto occupied = get(jobs.enqueue(child, profiles));
    require(occupied.tasks.front().state == "pending", "enabled producer did not admit artifact without local dependency");
    auto upgraded = bundle;
    upgraded.processor_version = "decoder-2-encoder-1";
    upgraded.files.front().checksum = std::string(64, 'd');
    upgraded.files.back().checksum = std::string(64, 'e');
    require(!registry.record(*first, upgraded, 9, context).isSuccess(), "producer changed occupied artifact version");
    get(jobs.cancel(occupied.operation_id, {}));
    get(registry.record(*first, upgraded, 9, context));
    require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(source) + " AND content_generation=2") == 2, "producer upgrade did not advance both identities atomically");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE id=" + retained + " AND status=2 AND local_path='fixture/previous-artifact' AND local_content_generation=1") == 1, "producer upgrade erased last local confirmation");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song_artifact' AND resource_id=" + retained + " AND cloud_provider='baidu' AND confirmed_content_generation=1 AND confirmed_target_revision=2") == 1, "producer upgrade rewrote historical cloud version");
    get(jobs.cancel(operation.operation_id, {}));
    require(!registry.record(*first, upgraded, 9, context).isSuccess(), "cancelled parent admitted late producer output");
    execute(pool, "DELETE FROM song_resources WHERE id=" + std::to_string(source));
    require(!registry.record(*second, upgraded, 9, context).isSuccess(), "deleted parent admitted producer output");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(source)) == 0, "deleted parent left indexed artifact rows");
}
} // namespace
// 验证双端事实归一、冲突留存、排除策略及有占用目标不得切换。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_lifecycle_"), "refusing non-isolated lifecycle database");
        auto pool = support::makeCoverTestPool();
        mysql::MySqlMetadataBatchWriter writer(pool);
        mysql::MySqlLegacyResourceImport importer(pool);
        for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) require(importer.configureTarget(destination, 1, std::string(64, char('a' + static_cast<int>(destination)))).isSuccess(), "target fixture failed");
        for (int scenario = 0; scenario < 2; ++scenario) {
            const auto suffix = std::to_string(scenario);
            auto material = album("lifecycle-material-" + suffix);
            require(save(writer, material), "material insert failed");
            const auto old = resource(pool, material.source_album_id);
            verified(pool, old, 'a');
            execute(pool, "UPDATE album_resources SET local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='reviewed',local_skip_reason='legacy_both' WHERE id=" + std::to_string(old));
            for (const auto provider : {"baidu", "gdrive"}) execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,album_resource_id,task_state,content_generation,target_revision,cloud_path,size_bytes,checksum,confirmed_content_generation,confirmed_target_revision,verification,verification_method,source_adapter,transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason) VALUES('album'," + std::to_string(old) + ",'" + provider + "'," + std::to_string(old) + ",'succeeded',1,1,'fixture/remote',3,REPEAT('a',64),1,1,'sha256_verified','remote_sha256','rclone','skip',1,1,'reviewed','legacy_both')");
            execute(pool, "UPDATE resource_cloud_receipts SET target_revision=7,confirmed_target_revision=7,policy_target_revision=7 WHERE resource_id=" + std::to_string(old) + " AND cloud_provider='gdrive'");
            auto canonical = album("lifecycle-canonical-" + suffix);
            if (scenario) {
                require(save(writer, canonical), "canonical insert failed");
                verified(pool, resource(pool, canonical.source_album_id), 'b');
                const auto existing = std::to_string(resource(pool, canonical.source_album_id));
                execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,album_resource_id,cloud_path,checksum) VALUES('album'," + existing + ",'baidu'," + existing + ",'fixture/canonical',REPEAT('b',64))");
            }
            canonical.source_material_id = material.source_album_id;
            canonical.source_album_kind = 2;
            canonical.source_resource_type = "5";
            canonical.detail_sync_status = 2;
            canonical.cover_url.clear();
            require(save(writer, canonical), "v2 alias merge failed");
            const auto current = resource(pool, canonical.source_album_id);
            require(scalar(pool, "SELECT COUNT(*) FROM album_resources WHERE id=" + std::to_string(old)) == 0, "retired alias resource remains");
            require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE resource_id=" + std::to_string(old) + " AND disposition='alias_retired'") == 1, "alias cloud evidence disappeared");
            if (!scenario) {
                require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='album' AND resource_id=" + std::to_string(current) + " AND checksum=REPEAT('a',64) AND transfer_policy='skip'") == 2, "matching content did not preserve both platforms");
                require(scalar(pool, "SELECT local_skip_generation FROM album_resources WHERE id=" + std::to_string(current)) > 0, "matching alias lost local skip");
                require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='album' AND resource_id=" + std::to_string(current) + " AND cloud_provider='gdrive' AND target_revision=7 AND confirmed_target_revision=7 AND policy_target_revision=7") == 1, "alias merge promoted a receipt to another target revision");
            } else {
                require(scalar(pool, "SELECT COUNT(*) FROM resource_baseline_blocks WHERE owner_type='album' AND resource_id=" + std::to_string(current) + " AND reason='alias_identity_unverified'") == 1, "unverified alias merge allowed implicit transfer");
                require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='album' AND resource_id=" + std::to_string(current)) == 1, "conflicting reference inherited false cloud confirmation");
                require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='album' AND resource_id=" + std::to_string(current) + " AND cloud_provider='baidu' AND checksum=REPEAT('b',64)") == 1, "canonical receipt was overwritten by alias conflict");
            }
        }
        auto excluded = album("lifecycle-excluded");
        require(save(writer, excluded), "excluded alias fixture failed");
        const auto excluded_id = resource(pool, excluded.source_album_id);
        execute(pool, "INSERT INTO resource_baseline_blocks(owner_type,resource_id,content_generation,baseline_id,reason) VALUES('album'," + std::to_string(excluded_id) + ",1,'reviewed','historical_scope_excluded')");
        auto included = album("lifecycle-excluded-canonical");
        included.source_material_id = excluded.source_album_id;
        included.source_album_kind = 2;
        included.source_resource_type = "5";
        included.detail_sync_status = 2;
        included.cover_url.clear();
        require(save(writer, included), "excluded alias merge failed");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_baseline_blocks WHERE owner_type='album' AND resource_id=" + std::to_string(resource(pool, included.source_album_id)) + " AND reason='historical_scope_excluded' AND baseline_id='reviewed'") == 1, "alias merge lost current exclusion intent");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE resource_id=" + std::to_string(excluded_id) + " AND disposition='alias_retired'") == 1, "excluded alias intent was not archived");
        auto active = album("lifecycle-active");
        require(save(writer, active), "active material fixture failed");
        const auto active_id = resource(pool, active.source_album_id);
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64)");
        model::ResourceTransferTargetProfile profile;
        profile.destination = model::ResourceDestination::Baidu;
        profile.fingerprint = std::string(64, 'b');
        profile.adapter = "rclone";
        model::ResourceTransferRequest request;
        request.resources = {{model::ResourceOwner::Album, active_id}};
        request.destinations = {model::ResourceDestination::Baidu};
        mysql::MySqlResourceTransferJobs jobs(pool);
        const auto pending = jobs.enqueue(request, {profile});
        require(pending.isSuccess() && pending.data()->tasks.front().state == "pending", "pending target fixture failed");
        auto merge = album("lifecycle-active-canonical");
        merge.source_material_id = active.source_album_id;
        merge.source_album_kind = 2;
        merge.source_resource_type = "5";
        merge.detail_sync_status = 2;
        merge.cover_url.clear();
        require(!save(writer, merge), "active alias transfer was deleted by merge");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources WHERE id=" + std::to_string(active_id)) == 1, "failed merge lost original resource");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance'");
        require(!importer.configureTarget(model::ResourceDestination::Baidu, 1, std::string(64, 'd')).isSuccess(), "target switch discarded pending transfer binding");
        require(jobs.cancel(pending.data()->operation_id, {}).isSuccess(), "pending cancellation failed");
        require(importer.configureTarget(model::ResourceDestination::Baidu, 1, std::string(64, 'd')).isSuccess(), "idle target could not advance revision");
        producer(pool);
        std::cout << "PASS: alias dual receipts, conflict retention, active ownership and target switch\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "lifecycle test failed: " << error.what() << '\n';
        return 1;
    }
}
