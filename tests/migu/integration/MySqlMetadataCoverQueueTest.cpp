#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "migu/support/MetadataCoverTestSupport.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <boost/asio/use_awaitable.hpp>

#include <cctype>
#include <future>
#include <iostream>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace sql = c::infrastructure::music::persistence::sql;

namespace {
// 执行测试专属 SQL，不接受外部输入。
void execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &statement)
{
    (void)support::runOnExecutor(pool->executor(), pool->coExecute(statement), "cover fixture SQL");
}

// 新迁移的全部语句在同一连接重跑，验证 prepared DDL 的可重入性。
boost::asio::awaitable<bool> replayMigration(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    for (const auto &statement : sql::SqlFileLoader::splitStatements(sql::SqlFileLoader::loadOrThrow("sql/mysql/migrations/V2026102802__metadata_cover_download_claims.sql"))) {
        co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    }
    co_return true;
}
} // namespace

// 默认执行完整独立库回归；创建空库模式供历史 migration 测试准备隔离环境。
int main(int argc, char *argv[])
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--create-db-only") {
            const auto config = support::makeMySqlConfigFromEnv();
            boost::asio::io_context context;
            test::require(support::runInIoContext(context, support::createCoverTestDatabase(config), "create empty isolated cover database"), "empty cover database creation failed");
            std::cout << "PASS: isolated empty test database prepared\n";
            return 0;
        }
        auto pool = support::makeCoverTestPool();
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        const auto schema = catalog.schemaStatus();
        test::require(schema.ok && schema.latest_available_version == 2026102901 && schema.latest_applied_version >= 2026102901, "cover schema status is stale or inconsistent");
        test::require(support::runOnExecutor(pool->executor(), replayMigration(pool), "replay cover migration"), "cover migration could not be replayed");
        auto queue = std::make_shared<mysql::MySqlMetadataCoverQueue>(pool);
        mysql::MySqlMetadataBatchWriter writer(pool);
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "local-cover-singer";
        singer.name = "封面本地化歌手";
        singer.avatar_url = "https://example.test/singer.png";
        model::AlbumSourceMetadata album;
        album.source_album_id = "local-cover-material";
        album.title = "封面本地化专辑";
        album.source_album_kind = 1;
        album.source_resource_type = "2003";
        album.cover_url = "https://example.test/album.png";
        model::MetadataWriteBatch batch;
        batch.singers = {singer};
        batch.albums = {album};
        test::require(writer.saveBatch(batch).isSuccess(), "metadata URL fixture failed");
        model::MetadataCoverQuery query;
        auto singers = queue->list(query);
        test::require(singers.isSuccess() && singers.data()->items.size() == 1, "singer cover URL not queryable");
        auto target = singers.data()->items.front();
        test::require(target.remote_url == singer.avatar_url && target.file.status == 0 && target.file.local_path.empty(), "metadata ingestion marked cover downloaded");
        // 同一快照在真实 MySQL 两个连接上竞争。
        auto first = std::async(std::launch::async, [&] { return queue->claim(target, false, "cover-one"); });
        auto second = std::async(std::launch::async, [&] { return queue->claim(target, false, "cover-two"); });
        const auto a = first.get(), b = second.get();
        test::require(a.isSuccess() && b.isSuccess() && static_cast<int>(a.data()->has_value()) + static_cast<int>(b.data()->has_value()) == 1, "MySQL cover claim was not exclusive");
        const auto stale = a.data()->has_value() ? **a.data() : **b.data();
        execute(pool, "UPDATE singer_resources SET claimed_at=UNIX_TIMESTAMP()-3600,updated_at=UNIX_TIMESTAMP() WHERE id=" + std::to_string(target.resource_id));
        const auto recovered = queue->recover(query, 60);
        test::require(recovered.isSuccess() && *recovered.data() == 1, "stale cover claim did not recover");
        model::MetadataResourceFile invalid_old_file;
        invalid_old_file.local_path = "migu/singers/fixture/stale.png";
        invalid_old_file.status = 2;
        test::require(!*queue->succeed(stale, invalid_old_file).data(), "old MySQL generation wrote success after recovery");

        test::TempRoot root;
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto http = std::make_shared<test::Http>();
        auto store = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        use::MetadataCoverDownloadWorkflow workflow(queue, store, store);
        model::MetadataCoverOptions options;
        auto downloaded = workflow.process(**queue->find(target.owner, target.resource_id).data(), options);
        test::require(downloaded.success && store->read(downloaded.record.file).has_value(), "MySQL-to-local cover download failed");
        mysql::MySqlMetadataCoverQueue independent(pool);
        const auto stored = **independent.find(target.owner, target.resource_id).data();
        test::require(stored.file.status == 2 && stored.file.local_path == downloaded.record.file.local_path && stored.file.checksum == downloaded.record.file.checksum, "independent query lost cover receipt");
        const int calls = http->calls;
        test::require(workflow.process(stored, options).skipped && http->calls == calls, "successful MySQL cover was downloaded again");
        options.refresh = true;
        http->status = 404;
        const auto failed = workflow.process(stored, options);
        test::require(!failed.success && failed.record.available && (**queue->find(target.owner, target.resource_id).data()).file.local_path == stored.file.local_path, "failed MySQL refresh destroyed old receipt");
        http->status = 200;
        http->on_get = [&] { execute(pool, "UPDATE singer_resources SET remote_url='https://example.test/NEW.png' WHERE id=" + std::to_string(target.resource_id)); };
        downloaded = workflow.process(**queue->find(target.owner, target.resource_id).data(), options);
        test::require(!downloaded.success && downloaded.record.file.result_code == "cover_claim_changed" && files->exists(stored.file.local_path), "old URL snapshot was allowed to replace the MySQL receipt");
        http->on_get = {};

        query.owner = model::CoverOwner::Album;
        auto albums = queue->list(query);
        test::require(albums.isSuccess() && albums.data()->items.size() == 1, "album cover URL not queryable");
        const auto album_target = albums.data()->items.front();
        options.refresh = false;
        auto album_file = workflow.process(album_target, options);
        test::require(album_file.success && album_file.record.file.local_path.starts_with("migu/albums/"), "album file ownership was wrong");
        // 活跃物料来源并入规范来源：旧 worker 失效，旧成功图片仍可读。
        auto active = queue->claim(**queue->find(model::CoverOwner::Album, album_target.resource_id).data(), true, "material-refresh");
        test::require(active.isSuccess() && active.data()->has_value(), "album refresh could not be claimed");
        auto canonical = album;
        canonical.source_album_id = "local-cover-canonical";
        canonical.source_material_id = album.source_album_id;
        canonical.source_album_kind = 2;
        canonical.source_resource_type = "5";
        canonical.detail_sync_status = 2;
        canonical.cover_url.clear();
        model::MetadataWriteBatch merge;
        merge.albums = {canonical};
        test::require(writer.saveBatch(merge).isSuccess(), "active album alias could not be merged");
        albums = queue->list(query);
        test::require(albums.isSuccess() && albums.data()->items.size() == 1 && albums.data()->items.front().source_id == canonical.source_album_id && albums.data()->items.front().file.status == 2 && albums.data()->items.front().file.local_path == album_file.record.file.local_path, "alias merge inherited a running claim or lost the successful file");
        test::require(queue->inspectWrite(model::CoverOwner::Album, album_target.resource_id, active.data()->value().claim_generation, album_file.record.file.local_path) == model::ResourceWriteDisposition::Committed, "recovery would delete a receipt moved to canonical album");
        test::require(!*queue->succeed(**active.data(), invalid_old_file).data(), "old material worker wrote after alias merge");
        test::require(support::runOnExecutor(pool->executor(), replayMigration(pool), "replay populated cover migration"), "populated migration replay failed");
        test::require((**queue->find(stored.owner, stored.resource_id).data()).file.local_path == stored.file.local_path, "migration replay changed existing file receipt");
        pool->cancel();
        std::cout << "PASS: isolated MySQL migration/replay, actual claims, generation recovery, local files, URL race and active album alias merge\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
