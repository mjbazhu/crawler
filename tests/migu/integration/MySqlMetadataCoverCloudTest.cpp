#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"
#include "migu/support/MetadataCoverCloudTestSupport.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <boost/asio/use_awaitable.hpp>

#include <future>
#include <iostream>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace use = c::application::use_cases;

namespace {
// 测试专属固定 SQL，数据库已由共用 helper 验证为隔离库。
void execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &sql)
{
    (void)support::runOnExecutor(pool->executor(), pool->coExecute(sql), "cover cloud fixture SQL");
}

// 连同认领状态比较确认事实，拒绝旧协议时不允许暗中清空或降级任何回执字段。
std::string receiptSnapshot(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &identity)
{
    auto result = support::runOnExecutor(pool->executor(), pool->coExecute("SELECT CAST(JSON_OBJECT('adapter',source_adapter,'path',cloud_path,'size',size_bytes,'checksum',checksum,'content',confirmed_content_generation,'target',confirmed_target_revision,'uploaded',uploaded_at,'verified',verified_at,'verification',verification,'method',verification_method,'algorithm',remote_hash_algorithm,'remote_hash',remote_hash_value,'evidence',evidence,'state',task_state,'generation',claim_generation,'attempt_count',attempt_count,'attempt',attempt,'batch',running_batch_id) AS CHAR) FROM resource_cloud_receipts WHERE " + identity), "cover receipt snapshot");
    test::require(result.rows().size() == 1, "strong receipt fixture was not unique");
    return mysql::MySqlValueReader::stringAt(result.rows().front(), 0);
}

// 新协议的确认只能由新协议更新；旧单端不能以目标或adapter变化为由覆盖它。
void protectUnifiedConfirmation(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::shared_ptr<mysql::MySqlMetadataCoverQueue> &queue, model::MetadataCoverRecord target)
{
    const auto identity = "owner_type='" + std::string(model::coverOwnerName(target.owner)) + "' AND resource_id=" + std::to_string(target.resource_id) + " AND cloud_provider='baidu'";
    const model::CloudFileReceipt weak {"baidupcs", "/LegacyChanged/" + target.file.local_path, target.file.size_bytes, target.file.checksum};
    auto claim = queue->claimCloud(target, weak, "late-legacy-confirmation");
    test::require(claim.isSuccess() && claim.data()->has_value(), "late legacy confirmation fixture could not claim");
    execute(pool, "UPDATE resource_cloud_receipts SET source_adapter='rclone',cloud_path='unified:verified/cover',size_bytes=" + std::to_string(target.file.size_bytes) + ",checksum='" + target.file.checksum + "',confirmed_content_generation=content_generation,confirmed_target_revision=target_revision,uploaded_at=17,verified_at=19,verification='sha256_verified',verification_method='remote_sha256',remote_hash_algorithm='sha256',remote_hash_value=checksum,evidence=JSON_OBJECT('unified_confirmation',CAST('true' AS JSON)) WHERE " + identity);
    auto before = receiptSnapshot(pool, identity);
    auto late = queue->succeedCloud(**claim.data(), weak);
    test::require(late.isSuccess() && !*late.data(), "late legacy completion downgraded unified SHA-256 confirmation");
    test::require(receiptSnapshot(pool, identity) == before, "late legacy completion modified strong confirmation facts or claim");
    execute(pool, "UPDATE resource_cloud_receipts SET task_state='succeeded',attempt=NULL,running_batch_id='',claimed_at=0 WHERE " + identity);
    target = **queue->find(target.owner, target.resource_id).data();
    before = receiptSnapshot(pool, identity);
    auto refused = queue->claimCloud(target, weak, "must-not-replace-strong");
    test::require(refused.isSuccess() && !refused.data()->has_value(), "changed legacy adapter or prefix reclaimed a strong confirmation");
    test::require(receiptSnapshot(pool, identity) == before, "refused legacy claim changed strong confirmation facts");
    execute(pool, "UPDATE resource_cloud_receipts SET verification='hash_matched',verification_method='unified_remote_hash',remote_hash_algorithm='md5',remote_hash_value=REPEAT('a',32) WHERE " + identity);
    target = **queue->find(target.owner, target.resource_id).data();
    before = receiptSnapshot(pool, identity);
    refused = queue->claimCloud(target, weak, "must-not-replace-unified");
    test::require(refused.isSuccess() && !refused.data()->has_value(), "legacy claim replaced another protocol's confirmed receipt");
    test::require(receiptSnapshot(pool, identity) == before, "legacy claim erased unified hash evidence");
}

// 收缩后的重复初始化沿正式ledger执行，不能重放会重新引入已退役列的旧DDL。
boost::asio::awaitable<bool> replay(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    co_return co_await mysql::MySqlMigrationRunner::coRun(connection, "sql/mysql/migrations");
}
} // namespace

// 真 MySQL 验证两类资源的独立认领、快照条件确认、失败和恢复，不连接任何云账号。
int main()
{
    try {
        auto pool = support::makeCoverTestPool();
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        const auto schema = catalog.schemaStatus();
        test::require(schema.ok && schema.latest_available_version == 2026102913 && schema.latest_applied_version == 2026102913, "cloud schema status or checksum mismatch");
        mysql::MySqlMetadataBatchWriter writer(pool);
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "cloud-cover-singer";
        singer.name = "云端封面测试歌手";
        singer.avatar_url = "https://example.test/singer.png";
        model::AlbumSourceMetadata album;
        album.source_album_id = "cloud-cover-material";
        album.title = "云端封面测试专辑";
        album.source_album_kind = 1;
        album.source_resource_type = "2003";
        album.cover_url = "https://example.test/album.png";
        model::MetadataWriteBatch batch;
        batch.singers = {singer};
        batch.albums = {album};
        test::require(writer.saveBatch(batch).isSuccess(), "cloud metadata fixture failed");
        auto queue = std::make_shared<mysql::MySqlMetadataCoverQueue>(pool);
        test::TempRoot root;
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto http = std::make_shared<test::Http>();
        auto local = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        auto remote = std::make_shared<test::CloudStore>();
        use::MetadataCoverDownloadWorkflow download(queue, local, local);
        use::MetadataCoverCloudWorkflow cloud(queue, local, remote, 1000);
        model::MetadataCoverQuery query;
        model::MetadataCoverRecord saved_album;
        for (auto owner : {model::CoverOwner::Singer, model::CoverOwner::Album}) {
            query = {};
            query.owner = owner;
            auto page = queue->list(query);
            test::require(page.isSuccess() && page.data()->items.size() == 1, "cloud owner fixture not found");
            auto saved = download.process(page.data()->items.front(), {});
            test::require(saved.success, "local image not ready before cloud test");
            auto target = **queue->find(owner, saved.record.resource_id).data();
            const model::CloudFileReceipt receipt {remote->backend(), remote->prefix() + target.file.local_path, target.file.size_bytes, target.file.checksum};
            if (owner == model::CoverOwner::Singer) {
                const auto uncertain_target = queue->claimCloud(target, receipt, "write-error");
                test::require(uncertain_target.isSuccess() && uncertain_target.data()->has_value(), "cloud write-error fixture could not claim");
                execute(pool, "CREATE TRIGGER cover_reject_cloud_write BEFORE UPDATE ON resource_cloud_receipts FOR EACH ROW SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture cloud write failure'");
                const auto uncertain_write = queue->succeedCloud(**uncertain_target.data(), receipt);
                execute(pool, "DROP TRIGGER cover_reject_cloud_write");
                test::require(!uncertain_write.isSuccess(), "cloud SQL error without confirmed receipt became definite non-commit");
                model::CloudFileResult unknown;
                unknown.uncertain = true;
                test::require(queue->failCloud(**uncertain_target.data(), unknown).isSuccess(), "cloud write-error fixture cleanup failed");
                target = **queue->find(owner, target.resource_id).data();
                test::require(target.cloud.claim_generation == (**uncertain_target.data()).cloud.claim_generation && target.cloud.claim_generation > 0, "unified receipt numeric projection lost unsigned claim generation");
            }
            auto one = std::async(std::launch::async, [&] { return queue->claimCloud(target, receipt, "one"); });
            auto two = std::async(std::launch::async, [&] { return queue->claimCloud(target, receipt, "two"); });
            auto a = one.get(), b = two.get();
            test::require(a.isSuccess() && b.isSuccess(), "cloud claims failed to execute");
            test::require(int(a.data()->has_value()) + int(b.data()->has_value()) == 1, "cloud claim was not exclusive");
            const auto active = a.data()->has_value() ? **a.data() : **b.data();
            const auto table = owner == model::CoverOwner::Singer ? "singer_resources" : "album_resources";
            execute(pool, "UPDATE resource_cloud_receipts SET claimed_at=UNIX_TIMESTAMP()-3600 WHERE owner_type='" + std::string(model::coverOwnerName(owner)) + "' AND cloud_provider='baidu' AND resource_id=" + std::to_string(target.resource_id));
            test::require(*queue->recoverCloud(query, 60).data() == 1, "stale cloud claim not recovered");
            test::require(!*queue->succeedCloud(active, receipt).data(), "old cloud generation confirmed after recovery");
            target = **queue->find(owner, target.resource_id).data();
            test::require(target.cloud.status == 4 && target.file.status == 2 && local->read(target.file).has_value(), "unknown cloud recovery changed local completion");
            remote->fail = true;
            test::require(!cloud.process(target, 0, "").success, "failed remote was marked successful");
            target = **queue->find(owner, target.resource_id).data();
            test::require(target.cloud.status == 3 && target.file.status == 2, "cloud failure overwrote local state");
            remote->fail = false;
            test::require(cloud.process(target, 0, "").success, "cloud-only retry did not confirm receipt");
            target = **queue->find(owner, target.resource_id).data();
            test::require(target.cloud.status == 2 && target.cloud.receipt.path == receipt.path && target.cloud.receipt.checksum == target.file.checksum && target.cloud.uploaded_at > 0, "cloud receipt not independently readable");
            cloud.candidates(query);
            test::require(queue->list(query).data()->total == 0, "confirmed cloud file still appeared in default processing queue");
            query.cloud_prefix = "/Other/";
            test::require(queue->list(query).data()->total == 1, "changed target root did not select cloud-only candidate");
            auto google = std::make_shared<mysql::MySqlMetadataCoverQueue>(pool, model::ResourceDestination::Gdrive);
            auto gtarget = **google->find(owner, target.resource_id).data();
            test::require(gtarget.cloud.provider == "gdrive" && gtarget.cloud.status == 0, "cloud projection guessed provider from adapter");
            const model::CloudFileReceipt greceipt {"rclone", "drive:separate/" + gtarget.file.local_path, gtarget.file.size_bytes, gtarget.file.checksum};
            test::require(!google->claimCloud(target, greceipt, "wrong-platform").data()->has_value(), "Baidu record entered Google claim");
            test::require(!google->claimCloud(gtarget, receipt, "wrong-adapter").data()->has_value(), "Baidu adapter was assigned to Google provider");
            auto gactive = **google->claimCloud(gtarget, greceipt, "google").data();
            test::require(*google->succeedCloud(gactive, greceipt).data(), "Google independent receipt failed");
            test::require((**queue->find(owner, target.resource_id).data()).cloud.receipt.path == receipt.path, "Google completion overwrote Baidu receipt");
            auto gcurrent = **google->find(owner, target.resource_id).data();
            execute(pool, "UPDATE resource_destination_states SET target_revision=target_revision+1 WHERE destination='gdrive'");
            test::require(!google->claimCloud(gcurrent, greceipt, "old-target").data()->has_value(), "stale target revision claimed new destination");
            gtarget = **google->find(owner, target.resource_id).data();
            gactive = **google->claimCloud(gtarget, greceipt, "new-google-target").data();
            test::require(*google->succeedCloud(gactive, greceipt).data(), "new target revision did not confirm independently");
            gtarget = **google->find(owner, target.resource_id).data();
            auto old_source = **google->claimCloud(gtarget, greceipt, "old-source-url").data();
            execute(pool, "UPDATE " + std::string(table) + " SET remote_url=CONCAT(remote_url,'?retirement-source-change') WHERE id=" + std::to_string(target.resource_id));
            test::require(!*google->succeedCloud(old_source, greceipt).data(), "late cloud completion ignored source URL revision");
            model::CloudFileResult old_failure;
            old_failure.result_code = "fixture_old_source";
            test::require(!*google->failCloud(old_source, old_failure).data(), "late cloud failure cleared changed source intent");
            execute(pool, "UPDATE resource_cloud_receipts SET claimed_at=1 WHERE owner_type='" + std::string(model::coverOwnerName(owner)) + "' AND resource_id=" + std::to_string(target.resource_id) + " AND cloud_provider='gdrive'");
            test::require(*google->recoverCloud(query, 1).data() == 1, "changed source legacy intent was not preserved for recovery");
            gtarget = **google->find(owner, target.resource_id).data();
            gactive = **google->claimCloud(gtarget, greceipt, "new-source-url").data();
            test::require(*google->succeedCloud(gactive, greceipt).data(), "fresh source revision did not resume legacy verification");
            target = **queue->find(owner, target.resource_id).data();
            const auto identity = "owner_type='" + std::string(model::coverOwnerName(owner)) + "' AND resource_id=" + std::to_string(target.resource_id);
            execute(pool, "UPDATE resource_cloud_receipts SET task_state='uncertain',attempt=JSON_OBJECT('unified_intent',true) WHERE " + identity + " AND cloud_provider='gdrive'");
            gtarget = **google->find(owner, target.resource_id).data();
            test::require(!google->claimCloud(gtarget, greceipt, "must-not-replace-unified").data()->has_value(), "legacy cloud replaced unresolved unified intent");
            test::require(*google->recoverCloud(query, 1).data() == 0, "legacy recovery took unified intent");
            const auto baidu_during_unknown_google = **queue->find(owner, target.resource_id).data();
            const auto independent_baidu = queue->claimCloud(baidu_during_unknown_google, receipt, "independent-baidu");
            test::require(independent_baidu.data()->has_value() && *queue->succeedCloud(**independent_baidu.data(), receipt).data(), "Google unknown intent blocked independent Baidu completion");
            execute(pool, "UPDATE resource_cloud_receipts SET task_state='succeeded',attempt=NULL,transfer_policy='skip',policy_content_generation=content_generation,policy_target_revision=target_revision WHERE " + identity + " AND cloud_provider='gdrive'");
            gtarget = **google->find(owner, target.resource_id).data();
            test::require(!google->claimCloud(gtarget, greceipt, "must-not-clear-skip").data()->has_value(), "legacy cloud cleared existing skip");
            execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='normal',policy_content_generation=0,policy_target_revision=0 WHERE " + identity + " AND cloud_provider='gdrive'");
            target = **queue->find(owner, target.resource_id).data();
            if (owner == model::CoverOwner::Album)
                saved_album = target;
            else {
                auto active_cloud = **queue->claimCloud(target, receipt, "refresh-race").data();
                gtarget = **google->find(owner, target.resource_id).data();
                auto google_inflight = **google->claimCloud(gtarget, greceipt, "google-refresh-race").data();
                model::MetadataCoverOptions refresh;
                refresh.refresh = true;
                auto refreshed = download.process(target, refresh);
                test::require(refreshed.success, "local refresh failed during cloud claim");
                test::require(!*queue->succeedCloud(active_cloud, receipt).data(), "old upload confirmed after local refresh");
                test::require(!*google->succeedCloud(google_inflight, greceipt).data(), "old Google upload confirmed after local refresh");
                const auto google_after = **google->find(owner, target.resource_id).data();
                test::require(google_after.cloud.status == 0 && google_after.cloud.receipt.path == greceipt.path, "local refresh lost last Google confirmation");
                auto current = **queue->find(owner, target.resource_id).data();
                test::require(current.cloud.status == 0 && current.cloud.receipt.path == receipt.path, "refresh did not invalidate cloud intent or erased last receipt");
                test::require(refreshed.record.cloud.status == current.cloud.status && refreshed.record.cloud.receipt.path == current.cloud.receipt.path, "local refresh response retained a stale confirmed cloud status");
            }
        }
        // 规范专辑接收物料回执后，确认云端对象与本地文件一起保留。
        auto canonical = album;
        canonical.source_album_id = "cloud-cover-canonical";
        canonical.source_material_id = album.source_album_id;
        canonical.source_album_kind = 2;
        canonical.source_resource_type = "5";
        canonical.detail_sync_status = 2;
        canonical.cover_url.clear();
        model::MetadataWriteBatch merge;
        merge.albums = {canonical};
        test::require(writer.saveBatch(merge).isSuccess(), "cloud album alias merge failed");
        query = {};
        query.owner = model::CoverOwner::Album;
        auto canonical_file = queue->list(query).data()->items.front();
        test::require(canonical_file.source_id == canonical.source_album_id && canonical_file.cloud.status == 2 && canonical_file.file.local_path == saved_album.file.local_path && canonical_file.cloud.receipt.path == saved_album.cloud.receipt.path, "alias merge separated cloud receipt from local file");
        for (auto owner : {model::CoverOwner::Singer, model::CoverOwner::Album}) {
            query = {};
            query.owner = owner;
            protectUnifiedConfirmation(pool, queue, queue->list(query).data()->items.front());
        }
        saved_album = **queue->find(canonical_file.owner, canonical_file.resource_id).data();
        test::require(support::runOnExecutor(pool->executor(), replay(pool), "replay cloud migration"), "cloud migration replay failed");
        test::require((**queue->find(canonical_file.owner, canonical_file.resource_id).data()).cloud.receipt.path == saved_album.cloud.receipt.path, "cloud migration replay erased successful receipt");
        pool->cancel();
        std::cout << "PASS: isolated MySQL cloud migration/replay, both owners, claim race, stale recovery, conditional receipts, candidate filtering and album alias\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
