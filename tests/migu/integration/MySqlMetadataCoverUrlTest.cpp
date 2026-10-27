#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "migu/support/MySqlTestSupport.h"
#include "migu/support/MetadataTestDiagnostics.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace support = c::tests::migu::support;

namespace {

// 使用明确异常报告失败，不依赖可在 Release 中禁用的 assert。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 独立 SQL 核验提交结果，不读取 writer 或采集缓存。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &query)
{
    const auto result = support::runOnExecutor(pool->executor(), pool->coExecute(query), "cover URL verification");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

} // namespace

// 只访问隔离测试库；本测试不装配 HTTP client、文件存储或资源下载器。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_"), "refusing non-test database");
        auto pool = support::makeInitializedPool();
        auto writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        use::MetadataPersistenceService saver(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {writer, std::make_shared<support::MetadataTestDiagnostics>()});
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "981" + suffix;
        singer.name = "图片地址歌手";
        singer.avatar_url = "https://example.test/avatar.jpg";
        model::AlbumSourceMetadata album;
        album.source_album_id = "982" + suffix;
        album.title = "图片地址专辑";
        album.source_album_kind = 1;
        album.cover_url = "https://example.test/list.jpg";
        model::MetadataWriteBatch batch;
        batch.singers = {singer};
        batch.albums = {album};
        require(saver.saveSources(batch, 0) && saver.saveSources(batch, 0), "URL ingestion or repeated ingestion failed");
        const auto singer_where = "r.singer_source_ref_id=(SELECT id FROM singer_source_refs WHERE source='migu' AND source_singer_id='" + singer.source_singer_id + "')";
        const auto album_where = "r.album_source_ref_id=(SELECT id FROM album_source_refs WHERE source='migu' AND source_album_id='" + album.source_album_id + "')";
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources r WHERE " + singer_where + " AND remote_url='https://example.test/avatar.jpg' AND status=0 AND local_path IS NULL AND size_bytes=0 AND fetched_at=0") == 1, "singer URL was duplicated or marked downloaded");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources r WHERE " + album_where + " AND remote_url='https://example.test/list.jpg' AND status=0 AND local_path IS NULL AND fetched_at=0") == 1, "album URL was duplicated or marked downloaded");
        auto sparse = batch;
        sparse.singers.front().avatar_url.clear();
        sparse.albums.front().cover_url.clear();
        require(saver.saveSources(sparse, 0), "sparse metadata failed");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources r WHERE " + singer_where + " AND remote_url='https://example.test/avatar.jpg'") == 1, "empty response cleared a known singer URL");

        album.detail_sync_status = 2;
        album.cover_url = "https://example.test/detail.webp";
        require(saver.saveAlbumSources({album}, 0), "detail URL save failed");
        auto summary = album;
        summary.detail_sync_status = 0;
        summary.cover_url = "https://example.test/weaker.jpg";
        require(saver.saveAlbumSources({summary}, 0), "summary URL save failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources r WHERE " + album_where + " AND remote_url='https://example.test/detail.webp'") == 1, "summary replaced the detail URL");
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE singer_resources r SET status=2,local_path='existing/avatar.jpg',size_bytes=17,checksum='existing-checksum',fetched_at=123 WHERE " + singer_where), "seed existing file receipt");
        singer.avatar_url = "https://example.test/new-avatar.jpg";
        require(saver.saveSingerSources({singer}, 0), "singer URL refresh failed");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources r WHERE " + singer_where + " AND status=2 AND local_path='existing/avatar.jpg' AND size_bytes=17 AND checksum='existing-checksum' AND fetched_at=123 AND remote_url='https://example.test/new-avatar.jpg'") == 1, "URL refresh reset existing file state");

        // 新建查询仓储和连接池，地址必须能够脱离原用例和内存缓存读取。
        auto reader_pool = support::makeInitializedPool();
        mysql::MySqlMusicCatalogQueryRepository catalog(reader_pool);
        const auto stored_singer = catalog.findSinger("migu", singer.source_singer_id);
        const auto stored_album = catalog.findAlbum("migu", album.source_album_id);
        require(stored_singer && stored_singer->avatar_url == singer.avatar_url, "independent singer query lost stored URL");
        require(stored_album && stored_album->cover_url == album.cover_url, "independent album query lost stored URL");
        reader_pool->cancel();

        // 物料来源归一时，只有地址而未下载的资源也必须迁入规范来源。
        auto material = summary;
        material.source_album_id = "983" + suffix;
        material.cover_url = "https://example.test/material.webp";
        require(saver.saveAlbumSources({material}, 0), "material URL seed failed");
        auto canonical = material;
        canonical.source_album_id = "984" + suffix;
        canonical.source_material_id = material.source_album_id;
        canonical.source_album_kind = 2;
        canonical.source_resource_type = "5";
        canonical.detail_sync_status = 2;
        canonical.cover_url.clear();
        require(saver.saveAlbumSources({canonical}, 0), "URL-only material merge failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + canonical.source_album_id + "' AND r.remote_url='https://example.test/material.webp' AND r.status=0") == 1, "alias merge lost a pending cover URL");

        auto broken = batch;
        broken.singers.front().source_singer_id = "985" + suffix;
        broken.albums.front().source_album_id = "986" + suffix;
        broken.albums.front().artist_refs = {{"missing-" + suffix, "", "", "artist", 1, true, false}};
        require(!saver.saveSources(broken, 0), "invalid relationship was accepted");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id='" + broken.singers.front().source_singer_id + "'") == 0, "failed batch partially committed singer and URL");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + broken.albums.front().source_album_id + "'") == 0, "failed batch partially committed album and URL");
        pool->cancel();
        std::cout << "PASS: stored cover URLs, idempotency, sparse updates, detail priority, file-state preservation, independent reads, alias merge and atomic rollback\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
