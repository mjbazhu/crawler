#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"
#include "migu/support/MySqlTestSupport.h"

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace support = c::tests::migu::support;

namespace {

// 失败必须返回非零退出码，不依赖 assert 编译开关。
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

// 仅在新建的独立测试库内构造历史数据和调用真实持久化 adapter；不依赖网络。
class Fixture {
public:
    // 先验证历史升级，再使用同一测试库验证新写入及筛选。
    Fixture()
    {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_"), "refusing non-test database");
        const auto historical = std::filesystem::temp_directory_path() / (config.database + "_singer_migrations");
        std::filesystem::create_directories(historical);
        for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations")) {
            if (entry.is_regular_file() && entry.path().extension() == ".sql" && entry.path().filename().string() < "V2026102702")
                std::filesystem::copy_file(entry.path(), historical / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
        }
        auto old_config = config;
        old_config.migrations_path = historical.string();
        pool = std::make_shared<mysql::MySqlConnectionPool>(old_config);
        require(support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "historical schema"), "historical schema failed");
        require(scalar("SELECT COUNT(*) FROM singer_source_refs") == 0, "fixture requires a fresh test database");
        execute("INSERT INTO singers(name,normalized_name,gender) VALUES('历史同名','legacy-shared',2),('归属不明','legacy-unknown',1),('独立旧名','legacy-single',1),('分类冲突','legacy-conflict',3)");
        const std::array<std::string, 8> names {"shared", "shared", "shared", "shared", "unknown", "unknown", "single", "conflict"};
        for (std::size_t i = 0; i < names.size(); ++i)
            execute("INSERT INTO singer_source_refs(singer_db_id,source,source_singer_id) SELECT id,'migu','legacy-" + std::to_string(i) + "' FROM singers WHERE normalized_name='legacy-" + names[i] + "'");
        const std::array<std::string, 4> tabs {"huayu-group", "oumei-nv", "huayu-nan", "rihan-nv"};
        for (std::size_t i = 0; i < tabs.size(); ++i)
            membership("legacy-" + std::to_string(i), tabs[i]);
        membership("legacy-7", "huayu-nan");
        membership("legacy-7", "huayu-nv");
        const auto old_identity = scalar("SELECT SUM(id*100+singer_db_id) FROM singer_source_refs");
        pool->cancel();
        pool = support::makeInitializedPool();
        require(scalar("SELECT SUM(id*100+singer_db_id) FROM singer_source_refs") == old_identity, "migration changed source identity");
        const std::array<int, 8> expected {3, 2, 1, 2, 0, 0, 1, 0};
        for (std::size_t i = 0; i < expected.size(); ++i)
            require(scalar("SELECT source_gender FROM singer_source_refs WHERE source_singer_id='legacy-" + std::to_string(i) + "'") == expected[i], "migration restored the wrong source type");
        require(scalar("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ((table_name='singers' AND column_name IN('gender','status')) OR (table_name='singer_source_refs' AND column_name='metadata_status'))") == 0, "unused singer fields survived migration");
        require(scalar("SELECT COUNT(*) FROM singer_catalog_sources WHERE source_item_key='singer:legacy-0' AND source_gender=3") == 1, "compatibility view lost source columns");
        writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        catalog = std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool);
        queue = std::make_shared<mysql::MySqlSingerSourceSyncQueue>(pool);
    }

    // 独立 SQL 查询用于核对 adapter 实际保存的值。
    void execute(const std::string &query)
    {
        (void)support::runOnExecutor(pool->executor(), pool->coExecute(query), "singer identity SQL");
    }

    // 标量查询不依赖生产 DTO 映射，避免测试自证。
    std::uint64_t scalar(const std::string &query)
    {
        const auto result = support::runOnExecutor(pool->executor(), pool->coExecute(query), "singer identity verification");
        require(!result.rows().empty(), "verification returned no row");
        return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    }

    // 索引分类按来源 ID 归属，不按共享名称归属。
    void membership(const std::string &id, const std::string &tab)
    {
        execute("INSERT INTO singer_index_memberships(singer_source_ref_id,tab) SELECT id,'" + tab + "' FROM singer_source_refs WHERE source_singer_id='" + id + "'");
    }

    // 复用生产归一及事务写入，覆盖歌曲片段携带未知类型的路径。
    void save(model::MetadataWriteBatch batch)
    {
        const auto result = writer->saveBatch(c::application::use_cases::buildMetadataWriteBatch(std::move(batch)));
        require(result.isSuccess(), "metadata save failed: " + result.message());
    }

    std::shared_ptr<mysql::MySqlConnectionPool> pool;                 // 独立测试库连接池
    std::shared_ptr<mysql::MySqlMetadataBatchWriter> writer;          // 生产事务 writer
    std::shared_ptr<mysql::MySqlMusicCatalogQueryRepository> catalog; // 生产目录及治理查询
    std::shared_ptr<mysql::MySqlSingerSourceSyncQueue> queue;         // 生产同步队列
};

// 同名来源拥有不同平台 ID，名称池可以复用但来源身份和类型不得合并。
model::SingerSourceMetadata singer(const std::string &id, int gender, std::string source = "migu")
{
    model::SingerSourceMetadata value;
    value.source = std::move(source);
    value.source_singer_id = "live-" + id;
    value.name = "同名歌手回归";
    value.normalized_name = value.name;
    value.gender = static_cast<c::domain::Gender>(gender);
    value.source_country = "华语";
    return value;
}

// 正序、逆序、未知字段和更名均不能改变来源身份或串写其他同名来源。
void verifyWrites(Fixture &f)
{
    model::MetadataWriteBatch batch;
    batch.singers = {singer("a", 3), singer("b", 2), singer("c", 1), singer("d", 2), singer("a", 2, "other")};
    f.save(batch);
    const auto identity = f.scalar("SELECT id FROM singer_source_refs WHERE source='migu' AND source_singer_id='live-a'");
    std::reverse(batch.singers.begin(), batch.singers.end());
    f.save(batch);
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id LIKE 'live-%'") == 5, "same-name source identities merged or duplicated");
    require(f.scalar("SELECT COUNT(DISTINCT singer_db_id) FROM singer_source_refs WHERE source_singer_id LIKE 'live-%'") == 1, "name pool stopped reusing normalized names");
    const std::array<int, 4> expected {3, 2, 1, 2};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto found = f.catalog->findSinger("migu", "live-" + std::string(1, static_cast<char>('a' + i)));
        require(found && found->gender == expected[i], "same-name type leaked across source IDs");
    }
    require(f.catalog->findSinger("other", "live-a")->gender == 2, "source platform was ignored");

    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_content_id = "live-song";
    song.title = "同名关系回归歌曲";
    song.source_album_id = "live-album";
    song.source_album_name = "同名关系回归专辑";
    song.artist_refs = {{"live-a", "同名歌手回归", "", "artist", 1, true, true}, {"live-b", "同名歌手回归", "", "artist", 2, false, false}};
    model::AlbumSourceMetadata album;
    album.source = "migu";
    album.source_album_id = "live-album";
    album.title = song.source_album_name;
    album.artist_refs = song.artist_refs;
    batch = {};
    batch.songs = {song};
    batch.albums = {album};
    f.save(batch);
    f.save(batch);
    require(f.scalar("SELECT source_gender FROM singer_source_refs WHERE id=" + std::to_string(identity)) == 3, "unknown artist fragment erased a known type");
    for (const auto *table : {"song_artist_refs", "album_artist_refs"})
        require(f.scalar("SELECT COUNT(DISTINCT r.singer_source_ref_id) FROM " + std::string(table) + " r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source='migu' AND s.source_singer_id IN('live-a','live-b')") == 2, "same-name artist relationships merged");
    batch = {};
    auto renamed = singer("a", 0);
    renamed.name = renamed.normalized_name = "同名歌手更名";
    batch.singers = {renamed};
    f.save(batch);
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE id=" + std::to_string(identity) + " AND source='migu' AND source_singer_id='live-a' AND source_gender=3") == 1, "rename changed source identity or type");
    require(f.scalar("SELECT COUNT(*) FROM song_artist_refs WHERE singer_source_ref_id=" + std::to_string(identity)) == 1, "rename lost song relationship");
    batch.singers = {singer("rollback", 1), singer("invalid", 4)};
    require(!f.writer->saveBatch(batch).isSuccess(), "invalid source type was accepted");
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id IN('live-rollback','live-invalid')") == 0, "invalid batch committed partially");
}

// 目录快慢查询、补池认领/重置和同步预览都必须使用来源自己的类型。
void verifyQueries(Fixture &f)
{
    model::MiguSingerCatalogQuery query;
    query.keyword = "live-";
    query.gender = 2;
    auto page = f.catalog->listSingers(query);
    require(page.total == 2 && page.items.size() == 2, "catalog type filter used the common name");
    query.min_songs_count = 0;
    page = f.catalog->listSingers(query);
    require(page.total == 2 && page.items.size() == 2, "filtered catalog type filter used the common name");
    model::SingerSourceSyncQuery pending;
    pending.keyword = "live-";
    pending.gender = 2;
    require(f.queue->previewPendingSongs(pending).eligible_count == 2, "song preview used the wrong type");
    require(f.queue->previewPendingAlbums(pending).eligible_count == 2, "album preview used the wrong type");
    require(f.queue->claimPendingSongs(pending).size() == 2, "song queue claimed other same-name types");
    require(f.queue->claimPendingAlbums(pending).size() == 2, "album queue claimed other same-name types");
    model::MiguBackfillTargetQuery reset;
    reset.keyword = "live-";
    reset.gender = 2;
    reset.statuses = {1};
    require(f.catalog->resetSingerSongs(reset) == 2, "song reset affected other same-name types");
    require(f.catalog->resetSingerAlbums(reset) == 2, "album reset affected other same-name types");
    reset.statuses = {0};
    const auto songs = f.catalog->claimSingerSongs(reset);
    const auto albums = f.catalog->claimSingerAlbums(reset);
    require(songs.claimed_count == 2 && albums.claimed_count == 2, "backfill claim used the common name type");
    for (const auto &item : songs.singers)
        require(item.gender == 2 && (item.source_singer_id == "live-b" || item.source_singer_id == "live-d"), "backfill returned another source identity");
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE source='migu' AND source_singer_id IN('live-a','live-c') AND songs_sync_status=0 AND albums_sync_status=0") == 2, "type filter changed unrelated source state");
    const auto status = f.catalog->schemaStatus();
    require(status.pending_migrations.empty() && status.checksum_mismatches.empty(), "schema status did not recognize the migration");
}

} // namespace

// 只运行来源身份、类型归属及迁移回归，不进行 HTTP 或资源操作。
int main()
{
    try {
        Fixture fixture;
        verifyWrites(fixture);
        verifyQueries(fixture);
        std::cout << "[PASS] singer identity, type ownership, historical migration and source filters" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "[FAIL] " << error.what() << std::endl;
        return 1;
    }
}
