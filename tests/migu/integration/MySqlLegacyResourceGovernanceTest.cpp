#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"

#include <iostream>
#include <memory>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace test = orchestrator::modules::crawler::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
namespace {
// 设置旧原生状态，不读取或创建实际文件。
void oldSong(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::uint64_t id, int status)
{
    test::execute(pool, "UPDATE song_resources SET status=" + std::to_string(status) + ",local_path='retained/legacy.mrc',size_bytes=123,checksum='rclone:size:123',last_error='old failure',updated_at=1 WHERE id=" + std::to_string(id));
}
// 仅登记一个真实云端待处理槽，不执行网络或worker。
void busy(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, model::ResourceOwner owner, std::uint64_t id, std::string key)
{
    test::get(mysql::MySqlResourceTransferJobs(pool).enqueue(test::request(owner, id, std::move(key), {model::ResourceDestination::Baidu}), test::profiles()));
}
// 保存当前本地历史策略，原生状态变化不能解除该声明。
void skip(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &table, std::uint64_t id)
{
    test::execute(pool, "UPDATE " + table + " SET local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='reviewed-old',local_skip_reason='legacy_both_clouds_present' WHERE id=" + std::to_string(id));
}
// 显式四类资源身份中的专辑封面fixture。
std::uint64_t album(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, int number)
{
    model::AlbumSourceMetadata value;
    value.source_album_id = "governance-album-" + std::to_string(number);
    value.title = value.source_album_id;
    value.cover_url = "https://example.test/cover.png";
    model::MetadataWriteBatch batch;
    batch.albums = {value};
    test::get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
    return test::scalar(pool, "SELECT MAX(id) FROM album_resources");
}
// 两类封面恢复只修改旧空闲认领，不接管三端任务或抹掉历史声明。
void covers(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, bool singer)
{
    const auto owner = singer ? model::ResourceOwner::Singer : model::ResourceOwner::Album;
    const std::string table = singer ? "singer_resources" : "album_resources";
    const std::string name = singer ? "singer" : "album";
    const auto first = singer ? test::singer(pool, 101) : album(pool, 101);
    const auto history = singer ? test::singer(pool, 102) : album(pool, 102);
    const auto active = singer ? test::singer(pool, 103) : album(pool, 103);
    busy(pool, owner, active, table + "-busy");
    skip(pool, table, history);
    const auto ids = std::to_string(first) + "," + std::to_string(history) + "," + std::to_string(active);
    test::execute(pool, "UPDATE " + table + " SET status=1,claimed_at=1,local_path='retained/cover.png',checksum=REPEAT('a',64),size_bytes=123 WHERE id IN(" + ids + ")");
    test::execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + name + "_resource_id,task_state,content_generation,target_revision,claim_generation,claimed_at,running_batch_id,attempt,cloud_path,checksum,size_bytes) SELECT '" + name + "',id,'baidu',id,'running',content_generation,1,1,1,'governance-cover',JSON_OBJECT('legacy_cover',CAST('true' AS JSON)),'/retained/cloud.png',REPEAT('a',64),123 FROM " + table + " WHERE id IN(" + ids + ") ON DUPLICATE KEY UPDATE task_state='running',claim_generation=1,claimed_at=1,running_batch_id='unified-cover',attempt=JSON_OBJECT('unified_intent',CAST('true' AS JSON))");
    test::execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='skip',policy_content_generation=content_generation,policy_target_revision=target_revision,baseline_id='reviewed-old' WHERE owner_type='" + name + "' AND resource_id=" + std::to_string(history) + " AND cloud_provider='baidu'");
    mysql::MySqlMetadataCoverQueue queue(pool);
    model::MetadataCoverQuery query;
    query.owner = singer ? model::CoverOwner::Singer : model::CoverOwner::Album;
    test::require(test::get(queue.recover(query, 60)) == 1, "cover recovery changed protected or active claims");
    test::require(test::get(queue.recoverCloud(query, 60)) == 1, "cloud legacy recovery changed protected or active claims");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM " + table + " r JOIN resource_cloud_receipts c ON c.owner_type='" + name + "' AND c.resource_id=r.id AND c.cloud_provider='baidu' WHERE r.id=" + std::to_string(first) + " AND r.status=2 AND c.task_state='uncertain' AND r.local_path='retained/cover.png' AND c.cloud_path='/retained/cloud.png' AND r.checksum=REPEAT('a',64) AND r.size_bytes=123") == 1, "legacy cover recovery lost last complete receipt");
    test::require(test::scalar(pool, "SELECT COUNT(*) FROM " + table + " r JOIN resource_cloud_receipts c ON c.owner_type='" + name + "' AND c.resource_id=r.id AND c.cloud_provider='baidu' WHERE r.id IN(" + std::to_string(history) + "," + std::to_string(active) + ") AND r.status=1 AND c.task_state='running' AND r.claimed_at=1 AND c.claimed_at=1") == 2, "cover recovery invalidated history or new task");
}
} // namespace

int main()
{
    try {
        using namespace test;
        require(orchestrator::modules::crawler::tests::migu::support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_legacy_governance_"), "refusing non-isolated legacy governance database");
        auto pool = orchestrator::modules::crawler::tests::migu::support::makeCoverTestPool();
        require(scalar(pool, "SELECT COUNT(*) FROM songs") == 0, "legacy governance test requires empty database");
        activate(pool);
        std::vector<std::uint64_t> ids;
        for (int i = 1; i <= 7; ++i) ids.push_back(mrc(pool, i));
        busy(pool, model::ResourceOwner::Song, ids[2], "song-governance-busy");
        for (const auto id : ids) oldSong(pool, id, 3);
        skip(pool, "song_resources", ids[1]);
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,task_state,attempt) VALUES('song'," + std::to_string(ids[3]) + ",'gdrive'," + std::to_string(ids[3]) + ",'uncertain',JSON_OBJECT('retained',true))");
        oldSong(pool, ids[4], 4);
        execute(pool, "INSERT INTO resource_baseline_blocks(owner_type,resource_id,content_generation,baseline_id) VALUES('song'," + std::to_string(ids[5]) + ",1,'excluded-fixture')");
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        model::MiguResourceActionQuery query;
        query.source = "migu";
        query.resource_ids = ids;
        query.statuses = {3};
        query.limit = 100;
        const auto tasks = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks");
        const auto operations = scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations");
        require(catalog.retryResources(query) == 2, "legacy retry did not restrict updates to free unblocked resources");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id IN(" + std::to_string(ids[0]) + "," + std::to_string(ids[6]) + ") AND status=0") == 2, "retry did not preserve original reset-only semantics");
        for (const auto id : ids) {
            if (id != ids[4]) oldSong(pool, id, 3);
        }
        query.resource_ids.clear();
        require(catalog.resetResources(query) == 2, "filtered reset consumed limit on protected history");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id IN(" + std::to_string(ids[1]) + "," + std::to_string(ids[2]) + "," + std::to_string(ids[3]) + "," + std::to_string(ids[5]) + ") AND status=3 AND last_error='old failure'") == 4, "reset changed protected state or errors");
        for (const auto id : ids) {
            if (id != ids[4]) oldSong(pool, id, 1);
        }
        query.stale_after_seconds = 60;
        require(catalog.recoverStaleResources(query, "stale old claim") == 2, "old recovery stole unified or blocked claims");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE local_path='retained/legacy.mrc' AND size_bytes=123 AND checksum='rclone:size:123'") == ids.size(), "governance changed paths, sizes or weak checksums");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(ids[1]) + " AND status=1 AND local_skip_baseline='reviewed-old' AND local_skip_generation=content_generation") == 1, "history skip was cleared or reset");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == tasks && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == operations, "legacy retry/reset implicitly started transfer");
        orchestrator::modules::crawler::application::use_cases::CrawlerServiceDependencies dependencies;
        dependencies.persistence.catalog_query_repository = std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool);
        orchestrator::modules::crawler::application::use_cases::CrawlerService service(std::move(dependencies));
        query.resource_ids = ids;
        query.statuses.clear();
        const auto reset = service.resetMiguResources(query);
        require(reset.updated_count == 2 && reset.items.size() == ids.size(), "legacy service changed selected resource scope");
        require(reset.items[1].status == 1 && reset.items[1].reason == "protected_or_changed", "legacy service fabricated pending status for protected history");
        require(reset.items[2].status == 1 && reset.items[2].reason == "protected_or_changed", "legacy service fabricated reset success for another operation");
        covers(pool, true);
        covers(pool, false);
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 3, "governance released another operation's slot");
        std::cout << "PASS: original reset-only semantics, skip/blocks/slots/intents, retained receipts, song and both cover recoveries\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
