#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
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

// 正式迁移在同一连接重跑，验证已填充数据不受幂等 DDL 影响。
boost::asio::awaitable<bool> replay(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    namespace sql = c::infrastructure::music::persistence::sql;
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    for (const auto &statement : sql::SqlFileLoader::splitStatements(sql::SqlFileLoader::loadOrThrow("sql/mysql/migrations/V2026102901__metadata_cover_cloud_storage.sql"))) co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    co_return true;
}
} // namespace

// 真 MySQL 验证两类资源的独立认领、快照条件确认、失败和恢复，不连接任何云账号。
int main()
{
    try {
        auto pool = support::makeCoverTestPool();
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        const auto schema = catalog.schemaStatus();
        test::require(schema.ok && schema.latest_available_version == 2026102901 && schema.latest_applied_version == 2026102901, "cloud schema status or checksum mismatch");
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
            auto one = std::async(std::launch::async, [&] { return queue->claimCloud(target, receipt, "one"); });
            auto two = std::async(std::launch::async, [&] { return queue->claimCloud(target, receipt, "two"); });
            auto a = one.get(), b = two.get();
            test::require(a.isSuccess() && b.isSuccess() && int(a.data()->has_value()) + int(b.data()->has_value()) == 1, "cloud claim was not exclusive");
            const auto active = a.data()->has_value() ? **a.data() : **b.data();
            const auto table = owner == model::CoverOwner::Singer ? "singer_resources" : "album_resources";
            execute(pool, "UPDATE " + std::string(table) + " SET cloud_claimed_at=UNIX_TIMESTAMP()-3600 WHERE id=" + std::to_string(target.resource_id));
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
            if (owner == model::CoverOwner::Album)
                saved_album = target;
            else {
                auto active_cloud = **queue->claimCloud(target, receipt, "refresh-race").data();
                model::MetadataCoverOptions refresh;
                refresh.refresh = true;
                auto refreshed = download.process(target, refresh);
                test::require(refreshed.success, "local refresh failed during cloud claim");
                test::require(!*queue->succeedCloud(active_cloud, receipt).data(), "old upload confirmed after local refresh");
                auto current = **queue->find(owner, target.resource_id).data();
                test::require(current.cloud.status == 0 && current.cloud.receipt.path == receipt.path, "refresh did not invalidate cloud intent or erased last receipt");
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
