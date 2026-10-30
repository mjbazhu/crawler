#include "crawler/application/use_cases/ResourceTransferPolicy.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferAccess.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/application/use_cases/ResourceTransferPreflight.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace use = c::application::use_cases;

namespace {
// 固定断言不输出原数据库字段或配置。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 仅在已验证的独立空测试库执行 fixture。
void execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    (void)support::runOnExecutor(pool->executor(), pool->coExecute(std::move(statement)), "legacy import fixture");
}

// 读取测试专属计数，测试不使用文件或网盘接口。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    auto result = support::runOnExecutor(pool->executor(), pool->coExecute(std::move(statement)), "legacy import scalar");
    require(!result.rows().empty(), "import scalar returned no row");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 创建只有人工证据的显式计划，不把它当作内容核验。
model::LegacyResourceImportPlan plan(std::string id, char hash)
{
    model::LegacyResourceImportPlan value;
    value.run_id = std::move(id);
    value.manifest_hash = std::string(64, hash);
    value.operator_attested_both = true;
    value.legacy_rclone_provider = model::ResourceDestination::Gdrive;
    return value;
}

// 混合页的批量审计保留每行证据；最后一行失败也回滚前面的策略、回执和游标。
void batchAudit(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, mysql::MySqlLegacyResourceImport &importer)
{
    execute(pool, "UPDATE song_resources SET status=2,local_skip_baseline='batch-before'");
    auto page = *importer.preview(model::ResourceOwner::Song, 0, 0, 5000).data();
    require(page.rows.size() >= 3, "mixed batch requires three resources");
    const auto excluded = page.rows[1].identity.resource_id, conflict = page.rows[2].identity.resource_id;
    execute(pool, "UPDATE song_resources SET status=0 WHERE id=" + std::to_string(excluded));
    page = *importer.preview(model::ResourceOwner::Song, 0, 0, 5000).data();
    execute(pool, "UPDATE song_resources SET local_path='batch-conflict-after-preview' WHERE id=" + std::to_string(conflict));
    execute(pool, "CREATE TRIGGER import_batch_audit_refuse BEFORE INSERT ON resource_cloud_migration_audit FOR EACH ROW BEGIN IF NEW.run_id='receipt-batch-fault' AND NEW.resource_id=" + std::to_string(page.rows.back().identity.resource_id) + " THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture late audit rejection'; END IF; END");
    const auto failed = importer.importPage(plan("receipt-batch-fault", 'a'), page);
    execute(pool, "DROP TRIGGER import_batch_audit_refuse");
    require(!failed.isSuccess() && failed.error() == c::application::result::ServiceErrorType::InternalError, "late audit failure became success or unknown");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-batch-fault'") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id IN('receipt-batch-fault:baidu','receipt-batch-fault:gdrive')") == 0, "late batch audit failure committed rows or cursors");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE local_skip_baseline='batch-before'") == page.rows.size() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE baseline_id='receipt-batch-fault'") == 0, "late audit failure committed policy or receipts");
    const auto mixed_plan = plan("receipt-batch-mixed", 'b');
    const auto mixed = importer.importPage(mixed_plan, page);
    require(mixed.isSuccess() && mixed.data()->conflicts == 1 && mixed.data()->excluded_resources == 1 && mixed.data()->imported_resources == page.rows.size() - 2, "batch import changed mixed dispositions");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-batch-mixed'") == page.rows.size(), "batch audit lost or duplicated resources");
    for (const auto &row : page.rows) {
        const auto disposition = row.identity.resource_id == conflict ? "conflict" : row.status == 2 ? "imported" :
                                                                                                       "excluded";
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-batch-mixed' AND resource_id=" + std::to_string(row.identity.resource_id) + " AND source_identity='" + row.source_identity + "' AND content_generation=" + std::to_string(row.generation) + " AND SHA2(CAST(old_values AS CHAR),256)='" + row.values_digest + "' AND disposition='" + disposition + "'") == 1, "batch audit changed exact old JSON, identity or disposition");
    }
    const auto replay = importer.importPage(mixed_plan, page);
    require(replay.isSuccess() && replay.data()->already_applied && replay.data()->new_receipts == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-batch-mixed'") == page.rows.size(), "mixed batch replay changed evidence");
}

// 超过4MiB的同一页会写多个审计块，最后一块失败仍须回滚先前块及所有资源事实。
void auditChunks(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, mysql::MySqlLegacyResourceImport &importer)
{
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type,status,local_path,size_bytes,checksum) WITH RECURSIVE chunk_rows(n) AS(SELECT 1 UNION ALL SELECT n+1 FROM chunk_rows WHERE n<224) SELECT source.id,'audio',CONCAT('batch-audit-',n),2,REPEAT('x',20000),1,'rclone:size:1' FROM chunk_rows CROSS JOIN(SELECT MIN(id) AS id FROM song_source_refs) source");
    const auto page = *importer.preview(model::ResourceOwner::Song, 0, 0, 5000).data();
    std::size_t legacy_bytes = 0;
    for (const auto &row : page.rows) legacy_bytes += row.legacy_values.size();
    require(legacy_bytes > 4 * 1024 * 1024 && page.rows.size() <= 5000, "audit fixture did not cross the payload bound");
    execute(pool, "CREATE TRIGGER import_chunk_audit_refuse BEFORE INSERT ON resource_cloud_migration_audit FOR EACH ROW BEGIN IF NEW.run_id='receipt-chunk-fault' AND NEW.resource_id=" + std::to_string(page.rows.back().identity.resource_id) + " THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture last chunk rejection'; END IF; END");
    const auto failed = importer.importPage(plan("receipt-chunk-fault", 'c'), page);
    execute(pool, "DROP TRIGGER import_chunk_audit_refuse");
    require(!failed.isSuccess() && failed.error() == c::application::result::ServiceErrorType::InternalError, "last audit chunk failure became success or unknown");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-chunk-fault'") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id IN('receipt-chunk-fault:baidu','receipt-chunk-fault:gdrive')") == 0, "last chunk failure retained earlier audit chunks or cursors");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE format_type LIKE 'batch-audit-%' AND local_skip_baseline=''") == 224 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE baseline_id='receipt-chunk-fault'") == 0, "last chunk failure retained resource policies or receipts");
    const auto approved = plan("receipt-chunk-approved", 'd');
    const auto complete = importer.importPage(approved, page);
    require(complete.isSuccess() && complete.data()->conflicts == 0 && complete.data()->excluded_resources == 1 && complete.data()->imported_resources == page.rows.size() - 1, "multi-chunk import changed page dispositions");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-chunk-approved'") == page.rows.size() && scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE format_type LIKE 'batch-audit-%' AND local_skip_baseline='receipt-chunk-approved'") == 224, "multi-chunk import lost audits or local policies");
    const auto replay = importer.importPage(approved, page);
    require(replay.isSuccess() && replay.data()->already_applied && replay.data()->new_receipts == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-chunk-approved'") == page.rows.size(), "multi-chunk replay duplicated audit evidence");
}
} // namespace

// 真实 MySQL 检查双端历史跳过、快照冲突、游标续跑及事务回滚，所有文件路径均不存在。
int main()
{
    try {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_receipt_import_"), "refusing non-isolated import database");
        auto pool = support::makeCoverTestPool();
        require(scalar(pool, "SELECT COUNT(*) FROM songs") == 0, "legacy import test requires empty business tables");
        mysql::MySqlLegacyResourceImport importer(pool);
        for (auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive})
            require(importer.configureTarget(destination, 1, std::string(64, char('a' + static_cast<int>(destination)))).isSuccess(), "initial target registration failed");
        auto first = std::async(std::launch::async, [&] { return importer.configureTarget(model::ResourceDestination::Baidu, 1, std::string(64, 'd')); });
        auto second = std::async(std::launch::async, [&] { return importer.configureTarget(model::ResourceDestination::Baidu, 1, std::string(64, 'e')); });
        auto one = first.get(), two = second.get();
        require(int(one.isSuccess()) + int(two.isSuccess()) == 1, "concurrent target registration lost a revision");
        require(importer.configureTarget(model::ResourceDestination::Baidu, 2, std::string(64, 'b')).isSuccess(), "restore test target failed");

        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "legacy-import-song";
        song.source_content_id = "legacy-import-content";
        song.title = "历史回执接管";
        song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
        model::MetadataWriteBatch batch;
        batch.songs = {song};
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "legacy-import-singer";
        singer.name = "历史接管歌手";
        singer.avatar_url = "https://example.test/singer.png";
        batch.singers = {singer};
        model::AlbumSourceMetadata album;
        album.source_album_id = "legacy-import-album";
        album.title = "历史接管专辑";
        album.source_album_kind = 1;
        album.source_resource_type = "2003";
        album.cover_url = "https://example.test/album.png";
        batch.albums = {album};
        mysql::MySqlMetadataBatchWriter writer(pool);
        require(writer.saveBatch(batch).isSuccess(), "legacy import song fixture failed");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT source.id,'mrc','' FROM song_source_refs source WHERE source.source_content_id='legacy-import-content' ON DUPLICATE KEY UPDATE id=song_resources.id");
        execute(pool, "UPDATE song_resources SET status=2,local_path=CONCAT('missing/',id),size_bytes=123,checksum='rclone:size:123'");
        const auto archived_rows = importer.preview(model::ResourceOwner::Song, 0, 0, 5000);
        require(archived_rows.isSuccess(), "retired cloud fixture preview failed");
        for (const auto &row : archived_rows.data()->rows) {
            const auto backend = scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(row.identity.resource_id) + " AND resource_kind='audio'") ? "baidupcs" : "rclone";
            c::tests::retired::cloudArchive(pool, row.identity, {{"cloud_backend", backend}, {"cloud_path", "/old/" + std::to_string(row.identity.resource_id)}});
        }
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources") >= 2, "import fixtures are incomplete");
        auto preview = importer.preview(model::ResourceOwner::Song, 0, 0, 5000);
        require(preview.isSuccess(), preview.message().c_str());
        require(preview.data()->rows.size() >= 2, "bounded preview omitted fixture resources");
        auto page = *preview.data();
        const auto count = page.rows.size();
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills") == 0, "preview wrote tasks or receipts");
        const auto changed_id = page.rows.front().identity.resource_id;
        execute(pool, "UPDATE song_resources SET local_path='changed-after-preview' WHERE id=" + std::to_string(changed_id));
        const auto changed = importer.importPage(plan("receipt-changed", 'a'), page);
        require(changed.isSuccess() && changed.data()->conflicts == 1 && changed.data()->imported_resources == count - 1, "changed snapshot was silently attested");
        require(scalar(pool, "SELECT local_skip_generation FROM song_resources WHERE id=" + std::to_string(changed_id)) == 0, "conflicting row acquired local skip");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-changed' AND disposition='conflict'") == 1, "conflict was lost behind cursor");

        page = *importer.preview(model::ResourceOwner::Song, 0, 0, 5000).data();
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,task_state,content_generation,target_revision,confirmed_content_generation,confirmed_target_revision,verification,cloud_path,checksum,size_bytes) VALUES('song'," + std::to_string(changed_id) + ",'baidu'," + std::to_string(changed_id) + ",'succeeded',1,3,1,3,'sha256_verified','/verified/object',REPEAT('f',64),123)");
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id) SELECT 'song',id,'gdrive',id FROM song_resources ON DUPLICATE KEY UPDATE cloud_path=NULL,path_unresolved=0,source_adapter='',verification='unverified'");
        const auto stable_history = importer.preview(model::ResourceOwner::Song, 0, 0, 5000);
        require(stable_history.isSuccess() && stable_history.data()->rows.size() == page.rows.size(), "history projection scope changed with current receipts");
        for (std::size_t i = 0; i < page.rows.size(); ++i)
            require(stable_history.data()->rows[i].values_digest == page.rows[i].values_digest && stable_history.data()->rows[i].cloud_path == page.rows[i].cloud_path && stable_history.data()->rows[i].cloud_backend == page.rows[i].cloud_backend, "current receipts rewrote retired manifest evidence");
        execute(pool, "UPDATE resource_cloud_receipts SET evidence=JSON_OBJECT('preserved_key','kept') WHERE owner_type='song' AND resource_id=" + std::to_string(changed_id) + " AND cloud_provider='baidu'");
        const auto previous_count = scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts");
        const auto approved = plan("receipt-approved", 'b');
        auto imported = importer.importPage(approved, page);
        require(imported.isSuccess() && imported.data()->conflicts == 0 && imported.data()->imported_resources == count && imported.data()->new_receipts == count * 2 - previous_count, "dual attestation import failed");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE transfer_policy='skip' AND policy_content_generation=1 AND baseline_id='receipt-approved'") == count * 2, "legacy backend single value lost one platform");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE local_skip_generation=1 AND local_skip_target_revision=1 AND local_skip_baseline='receipt-approved'") == count, "local download was not suppressed");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE verification='sha256_verified' AND cloud_path='/verified/object' AND checksum=REPEAT('f',64)") == 1, "attestation downgraded verified receipt");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE verification='operator_attested' AND checksum='' AND uploaded_at IS NULL AND verified_at IS NULL") == count * 2 - 1, "migration manufactured content proof or timestamps");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND source_adapter='baidupcs'") == 0, "legacy adapter was attributed to the unrecorded platform");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND cloud_path IS NULL AND path_unresolved=1 AND source_adapter=''") == 1, "empty unconfirmed receipt manufactured a resolved path");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND cloud_path LIKE '/old/%' AND path_unresolved=0 AND source_adapter='rclone'") == 1, "known historical path did not fill an empty unconfirmed receipt");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + std::to_string(changed_id) + " AND cloud_provider='baidu' AND JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.preserved_key'))='kept'") == 1, "historical import overwrote unrelated evidence");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.retired_legacy.cloud_backend'))='rclone' AND uploaded_at IS NULL") == 1, "mapped legacy evidence was lost or became a new upload timestamp");
        const auto replay = importer.importPage(approved, page);
        require(replay.isSuccess() && replay.data()->already_applied && replay.data()->new_receipts == 0 && replay.data()->next_id == page.high_water_id, "same manifest/page was not idempotent");
        auto altered = approved;
        altered.manifest_hash = std::string(64, 'c');
        require(!importer.importPage(altered, page).isSuccess(), "existing run accepted a different manifest");

        use::ResourceTransferPolicyInput policy;
        policy.version = {1, 1};
        policy.skip = {{1, 1}, "receipt-approved", "legacy_both_clouds_present", true};
        policy.available = false;
        policy.dispatch_ready = false;
        require(use::evaluateResourceTransfer(policy) == use::ResourceWorkDecision::SkipPolicy, "imported local skip requires a file or online cloud");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE baseline_ready=0 AND dispatch_state='maintenance'") == 3, "page import silently activated worker dispatch");

        auto first_page = *importer.preview(model::ResourceOwner::Song, 0, 0, 1).data();
        auto paged = plan("receipt-paged", 'c');
        auto chunk = importer.importPage(paged, first_page);
        require(chunk.isSuccess(), "controlled import initial page failed");
        const auto stale_execution = paged;
        const auto paused = importer.control(paged, first_page, "pause");
        require(paused.isSuccess() && paused.data()->control_state == "paused" && paused.data()->control_generation == 1, "dual import pause did not advance control generation");
        const auto next_controlled = importer.preview(model::ResourceOwner::Song, chunk.data()->next_id, first_page.high_water_id, 1);
        require(next_controlled.isSuccess() && !importer.importPage(stale_execution, *next_controlled.data()).isSuccess(), "paused dual import accepted old page");
        paged.control_generation = 1;
        const auto repeated_pause = importer.control(paged, first_page, "pause");
        require(repeated_pause.isSuccess() && repeated_pause.data()->control_generation == 1, "idempotent pause advanced generation again");
        execute(pool, "CREATE TRIGGER import_control_refuse BEFORE INSERT ON governance_events FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture control rejection'");
        require(!importer.control(paged, first_page, "resume").isSuccess() && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id IN('receipt-paged:baidu','receipt-paged:gdrive') AND control_state='paused' AND control_generation=1") == 2, "dual control audit failure left diverged controls");
        execute(pool, "DROP TRIGGER import_control_refuse");
        const auto resumed = importer.control(paged, first_page, "resume");
        require(resumed.isSuccess() && resumed.data()->control_generation == 2 && !importer.importPage(stale_execution, *next_controlled.data()).isSuccess(), "resume adopted page from old invocation");
        paged.control_generation = 2;
        require(chunk.isSuccess(), "first bounded page failed");
        auto cursor = chunk.data()->next_id;
        while (cursor < first_page.high_water_id) {
            const auto next = importer.preview(model::ResourceOwner::Song, cursor, first_page.high_water_id, 1);
            require(next.isSuccess(), "resume preview failed");
            chunk = importer.importPage(paged, *next.data());
            require(chunk.isSuccess() && chunk.data()->next_id > cursor, "committed cursor did not advance");
            cursor = chunk.data()->next_id;
        }
        const auto older_replay = importer.importPage(paged, first_page);
        require(older_replay.isSuccess() && older_replay.data()->already_applied && older_replay.data()->next_id == cursor, "old page replay moved cursor backwards");
        const auto progress = importer.progress(paged, first_page);
        require(progress.isSuccess() && progress.data()->state == "completed" && progress.data()->cursor == cursor && progress.data()->processed_resources == count && progress.data()->imported_resources == count && progress.data()->conflicts == 0, "durable progress differs between providers");
        require(!importer.control(paged, first_page, "resume").isSuccess(), "completed dual import resumed");

        auto fault_page = *importer.preview(model::ResourceOwner::Song, 0, 0, 5000).data();
        execute(pool, "CREATE TRIGGER receipt_import_refuse BEFORE UPDATE ON song_resources FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture import rejection'");
        const auto fault = importer.importPage(plan("receipt-fault", 'd'), fault_page);
        execute(pool, "DROP TRIGGER receipt_import_refuse");
        require(!fault.isSuccess() && fault.error() == c::application::result::ServiceErrorType::InternalError, "definite SQL rejection became committed or unknown");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id IN ('receipt-fault:baidu','receipt-fault:gdrive')") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='receipt-fault'") == 0, "failed transaction advanced audit/cursor");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE local_skip_baseline='receipt-paged'") == count, "rollback changed local policy");

        // 另外三类归属走各自快照、主键和本地策略；不会把父 MRC 当成产物身份。
        execute(pool, "UPDATE singer_resources SET status=2,local_path='missing/singer.png',size_bytes=12,checksum='rclone:size:12'");
        execute(pool, "UPDATE album_resources SET status=2,local_path='missing/album.png',size_bytes=13,checksum=''");
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,local_path,size_bytes,checksum,status) SELECT id,'word_json',REPEAT('a',64),'fixture','missing/words.json',14,'rclone:size:14',2 FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        for (const auto &[owner, table] : {std::pair {model::ResourceOwner::Singer, "singer_resources"}, std::pair {model::ResourceOwner::Album, "album_resources"}, std::pair {model::ResourceOwner::SongArtifact, "song_resource_artifacts"}}) {
            const auto other = importer.preview(owner, 0, 0, 10);
            require(other.isSuccess() && other.data()->rows.size() == 1, "owner-specific snapshot lost its resource");
            const auto other_plan = plan("receipt-owner-" + std::string(model::resourceOwnerName(owner)), 'f');
            const auto result = importer.importPage(other_plan, *other.data());
            require(result.isSuccess() && result.data()->imported_resources == 1 && result.data()->new_receipts == 2, "owner-specific dual import failed");
            require(scalar(pool, "SELECT COUNT(*) FROM " + std::string(table) + " WHERE local_skip_generation=content_generation AND local_skip_target_revision=1 AND local_skip_baseline='" + other_plan.run_id + "'") == 1, "owner-specific local policy was not persisted");
        }
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song_artifact' AND artifact_resource_id=(SELECT id FROM song_resource_artifacts LIMIT 1) AND song_resource_id IS NULL") == 2, "artifact receipts reference the parent MRC");

        mysql::MySqlResourceTransferAccess access(pool);
        require(access.writesAllowed().isSuccess() && !*access.writesAllowed().data(), "default maintenance allowed source writes");
        for (auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            const auto decision = use::preflightResourceTransfer(&access, {model::ResourceOwner::Song, changed_id}, destination);
            require(decision.isSuccess() && decision.data()->decision == use::ResourceWorkDecision::SkipPolicy, "persisted historical skip requires a file or activated dispatch");
        }
        for (const auto &[owner, table] : {std::pair {model::ResourceOwner::Singer, "singer_resources"}, std::pair {model::ResourceOwner::Album, "album_resources"}, std::pair {model::ResourceOwner::SongArtifact, "song_resource_artifacts"}}) {
            const auto id = scalar(pool, "SELECT id FROM " + std::string(table) + " LIMIT 1");
            const auto decision = use::preflightResourceTransfer(&access, {owner, id}, model::ResourceDestination::Local);
            require(decision.isSuccess() && decision.data()->result_code == "skip_existing", "resource-specific admission lost local skip");
        }
        require(!access.inspect({model::ResourceOwner::Song, 0}, model::ResourceDestination::Local).isSuccess(), "invalid identity was admitted");
        require(access.inspect({model::ResourceOwner::Song, 999999}, model::ResourceDestination::Local).error() == c::application::result::ServiceErrorType::NotFound, "missing resource was not distinguished from permission");

        // 即使旧治理入口只重置状态，匹配本地策略也不能再次进入普通或关联认领。
        mysql::MySqlSongResourceQueue legacy_queue(pool);
        execute(pool, "UPDATE song_resources SET status=0");
        model::SongResourceClaimQuery legacy_query;
        legacy_query.resource_ids = {changed_id};
        require(legacy_queue.claimPending(legacy_query).empty(), "legacy ordinary claim ignored durable local skip");
        model::SongResourceRelatedClaimRequest related;
        related.song_source_ref_id = scalar(pool, "SELECT id FROM song_source_refs LIMIT 1");
        related.resource_kind = "mrc";
        related.remote_url = "https://example.test/never-requested";
        related.batch_id = "skip-must-survive-reset";
        require(!legacy_queue.claimRelated(related), "legacy related claim ignored durable local skip");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE status=1") == 0, "historical skip left a running legacy claim");
        execute(pool, "UPDATE song_resources SET status=2");

        // 正式组合根启用 SQL 级认领闸门，检查与 UPDATE 间的状态变化也不能绕过。
        mysql::MySqlSongResourceQueue::SqlPaths guarded_paths;
        guarded_paths.enforce_dispatch = true;
        mysql::MySqlSongResourceQueue guarded_queue(pool, guarded_paths);
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'trc','' FROM song_source_refs LIMIT 1");
        const auto fresh_id = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='trc' LIMIT 1");
        model::SongResourceClaimQuery fresh_query;
        fresh_query.resource_ids = {fresh_id};
        require(guarded_queue.claimPending(fresh_query).empty(), "SQL claim bypassed maintained dispatch");
        related.resource_kind = "lyric";
        require(!guarded_queue.claimRelated(related) && scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE resource_kind='lyric'") == 0, "maintained related claim created a resource");

        execute(pool, "UPDATE resource_destination_states SET dispatch_state='paused'");
        require(!*access.destinationReady(model::ResourceDestination::Local).data(), "paused target was ready");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1 WHERE destination='local'");
        require(*access.writesAllowed().data(), "paused unused targets blocked ready local writes");
        require(*access.destinationReady(model::ResourceDestination::Local).data() && guarded_queue.claimPending(fresh_query).size() == 1, "ready local could not claim a fresh resource");
        execute(pool, "UPDATE song_resources SET status=0 WHERE id=" + std::to_string(fresh_id));
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance' WHERE destination='gdrive'");
        require(!*access.writesAllowed().data(), "one maintained target failed to close source writes");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance',baseline_ready=0");

        require(importer.configureTarget(model::ResourceDestination::Gdrive, 1, std::string(64, 'd')).isSuccess(), "target switch failed");
        const auto wrong_target = importer.importPage(plan("receipt-wrong-target", 'e'), fault_page);
        require(!wrong_target.isSuccess() && wrong_target.error() == c::application::result::ServiceErrorType::Conflict, "old target manifest was reused");
        policy.version.target_revision = 2;
        require(use::evaluateResourceTransfer(policy) == use::ResourceWorkDecision::Blocked, "old skip followed target switch");
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='maintenance'");
        const auto cancel_page = importer.preview(model::ResourceOwner::Song, 0, 0, 1);
        auto cancel_plan = plan("receipt-cancel-control", 'a');
        require(cancel_page.isSuccess() && importer.importPage(cancel_plan, *cancel_page.data()).isSuccess(), "cancel fixture initial page failed");
        const auto cancelled = importer.control(cancel_plan, *cancel_page.data(), "cancel");
        require(cancelled.isSuccess() && cancelled.data()->control_state == "cancelled" && cancelled.data()->control_generation == 1, "dual import cancellation not atomic");
        cancel_plan.control_generation = 1;
        require(!importer.control(cancel_plan, *cancel_page.data(), "resume").isSuccess() && !importer.importPage(cancel_plan, *cancel_page.data()).isSuccess(), "cancelled dual import revived or accepted a page");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id IN('receipt-cancel-control:baidu','receipt-cancel-control:gdrive') AND control_state='cancelled' AND control_generation=1") == 2, "provider control states diverged on cancel");
        auto old_terminal_plan = plan("receipt-old-terminal", 'b');
        require(importer.importPage(old_terminal_plan, *cancel_page.data()).isSuccess(), "legacy terminal fixture failed");
        execute(pool, "UPDATE resource_cloud_backfills SET state='cancelled' WHERE run_id IN('receipt-old-terminal:baidu','receipt-old-terminal:gdrive')");
        require(!importer.control(old_terminal_plan, *cancel_page.data(), "pause").isSuccess() && !importer.control(old_terminal_plan, *cancel_page.data(), "resume").isSuccess() && !importer.importPage(old_terminal_plan, *cancel_page.data()).isSuccess(), "new default control revived legacy cancelled state");
        batchAudit(pool, importer);
        auditChunks(pool, importer);
        // 没有rclone平台映射时，两端都只保留声明skip，不投影旧云端位置或时间。
        const auto unmapped_singer = scalar(pool, "SELECT MIN(id) FROM singer_resources");
        c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, unmapped_singer}, {{"cloud_backend", "rclone"}, {"cloud_path", "archive:unknown-provider"}, {"cloud_uploaded_at", 1234}});
        auto unmapped_plan = plan("receipt-unmapped-retired", 'f');
        unmapped_plan.legacy_rclone_provider.reset();
        const auto unmapped_page = importer.preview(model::ResourceOwner::Singer, 0, 0, 500);
        require(unmapped_page.isSuccess() && importer.importPage(unmapped_plan, *unmapped_page.data()).isSuccess(), "unmapped legacy import failed");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='singer' AND resource_id=" + std::to_string(unmapped_singer) + " AND NOT JSON_CONTAINS_PATH(COALESCE(evidence,JSON_OBJECT()),'one','$.retired_legacy') AND uploaded_at IS NULL") == 2, "unmapped rclone evidence was attributed to a provider");
        pool->cancel();
        std::cout << "PASS: explicit dual attestation, no content I/O, local skip, strong proof preservation, atomic cursor/replay, SQL rollback and target revision isolation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
