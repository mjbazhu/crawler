#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"
#include "migu/support/MetadataTestDiagnostics.h"
#include "mocks/MockMusicProvider.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace storage = c::infrastructure::storage::filesystem;
namespace support = c::tests::migu::support;

namespace {

// 测试断言不依赖 Debug 的 assert 开关。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 只允许独立测试库；每次运行使用不同来源 ID，便于保留失败证据并重复执行。
class Fixture {
public:
    Fixture()
    {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_"), "refusing non-test database");
        prefix = "identity_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_";
        pool = support::makeInitializedPool();
        writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        queue = std::make_shared<mysql::MySqlAlbumSourceSyncQueue>(pool);
        directory = std::filesystem::temp_directory_path() / config.database / prefix;
        files = std::make_shared<storage::FileStorage>(directory.string());
        saver = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, std::make_shared<support::MetadataTestDiagnostics>()});
    }

    // 标量只读查询用于独立核验事务后的实际值。
    std::uint64_t scalar(const std::string &query)
    {
        auto result = support::runOnExecutor(pool->executor(), pool->coExecute(query), "identity verification");
        return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    }

    // 查询已登记的封面地址，核验归一后没有丢失来源地址。
    std::string cover(const std::string &album)
    {
        auto result = support::runOnExecutor(pool->executor(), pool->coExecute("SELECT r.remote_url FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + prefix + album + "'"), "cover verification");
        return mysql::MySqlValueReader::stringAt(result.rows().front(), 0);
    }

    // 文件数量用于确认元数据写入和回滚均不触发下载。
    std::size_t fileCount() const
    {
        std::size_t count = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
            if (entry.is_regular_file()) ++count;
        return count;
    }

    // 构造带关系顺序和封面的歌曲摘要，来源身份由测试后缀区分。
    model::SongSourceMetadata song(const std::string &suffix, const std::string &album) const
    {
        model::SongSourceMetadata value;
        value.source = "migu";
        value.source_content_id = prefix + suffix;
        value.title = "身份归一测试歌曲";
        value.source_album_id = prefix + album;
        value.source_album_name = "身份归一测试专辑";
        value.source_album_pinyin = "shouci";
        value.artist_refs = {{prefix + "singer", "测试入口歌手", "geshou", "artist", 1, true, true}};
        value.cover_url = "https://example.test/identity.png";
        value.album_disc_name = "Disc 2";
        value.album_disc_no = 2;
        value.album_track_no = 3;
        value.album_song_order = 4;
        return value;
    }

    // 显式物料映射来自数字专辑详情，声明数量和本次曲目结果分开保存。
    model::AlbumSourceMetadata digital(const std::string &suffix, const std::string &material) const
    {
        model::AlbumSourceMetadata value;
        value.source_album_id = prefix + suffix;
        value.source_material_id = prefix + material;
        value.title = "身份归一测试专辑";
        value.source_resource_type = "5";
        value.source_album_kind = 2;
        value.source_title_pinyin = "xiangxi";
        value.detail_sync_status = 2;
        value.total_count = "1";
        value.songs_sync = model::AlbumSongSyncResult {2, 1, {}};
        value.artist_refs = {{prefix + "guest", "测试专辑歌手", "", "artist", 1, true, false}};
        return value;
    }

    std::string prefix;                                      // 本次运行的唯一测试身份前缀
    std::filesystem::path directory;                         // 本次运行的独立文件目录
    std::shared_ptr<mysql::MySqlConnectionPool> pool;        // 测试数据库连接池
    std::shared_ptr<mysql::MySqlMetadataBatchWriter> writer; // 生产事务 writer
    std::shared_ptr<mysql::MySqlAlbumSourceSyncQueue> queue; // 生产同步状态 adapter
    std::shared_ptr<storage::FileStorage> files;             // 真实本地文件 adapter
    std::shared_ptr<use::MetadataPersistenceService> saver;  // 生产元数据持久化用例
};

// 先摘要后详情必须迁移关系、保留入口歌手和已有封面，后续摘要不能再次创建物料来源。
void verifyAliasMerge(Fixture &f)
{
    auto song = f.song("song", "material");
    require(f.saver->saveSongSources({song}, 1), "initial material summary failed");
    model::AlbumSourceMetadata material;
    material.source_album_id = song.source_album_id;
    material.title = song.source_album_name;
    material.source_album_kind = 1;
    material.artist_refs = song.artist_refs;
    require(f.saver->saveAlbumSources({material}, 1), "explicit material album artist seed failed");
    const auto path = f.cover("material");
    auto album = f.digital("canonical", "material");
    require(f.saver->saveAlbumSources({album}, 1), "digital detail failed");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "material'") == 0, "material source survived explicit identity merge");
    require(f.scalar("SELECT COUNT(*) FROM albums WHERE identity_hash=SHA2('migu|" + f.prefix + "material',256)") == 0, "unreferenced material album survived merge");
    require(f.scalar("SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + f.prefix + "canonical' AND r.disc_no=2 AND r.track_no=3 AND r.song_order=4") == 1, "alias migration lost song order");
    require(f.scalar("SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + f.prefix + "canonical'") == 2, "alias migration lost album artists");
    require(f.scalar("SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + f.prefix + "canonical' AND r.is_seed=1") == 1, "alias migration lost seed relationship");
    require(f.cover("canonical") == path, "alias merge discarded the retained cover");
    require(f.saver->saveSongSources({song}, 1), "mapped summary recollection failed");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id IN('" + f.prefix + "canonical','" + f.prefix + "material')") == 1, "mapped summary created a second source");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "canonical' AND source_title_pinyin='xiangxi' AND songs_sync_status=2 AND songs_sync_count=1") == 1, "summary downgraded detailed fields or sync result");

    auto conflicting = album;
    conflicting.source_album_id = f.prefix + "ambiguous";
    require(!f.saver->saveAlbumSources({conflicting}, 1), "ambiguous material mapping was accepted");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "ambiguous'") == 0, "ambiguous batch partially committed");
    auto unrelated = album;
    unrelated.source_album_id = f.prefix + "same-title";
    unrelated.source_material_id.clear();
    require(f.saver->saveAlbumSources({unrelated}, 1), "unrelated same-title album failed");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "same-title'") == 1, "same-title album was incorrectly merged");
}

// 归一之后发生关系失败时，旧来源、关系和文件必须随事务完整恢复。
void verifyMergeRollback(Fixture &f)
{
    auto song = f.song("rollback-song", "rollback-material");
    require(f.saver->saveSongSources({song}, 1), "rollback seed failed");
    const auto old_path = f.cover("rollback-material");
    const auto before = f.fileCount();
    model::MetadataWriteBatch batch;
    batch.albums = {f.digital("rollback-canonical", "rollback-material")};
    song.source_content_id = f.prefix + "invalid-song";
    song.artist_refs = {{f.prefix + "unknown", "", "", "artist", 1, true, false}};
    batch.songs = {song};
    require(!f.saver->saveSources(batch, 0), "broken merge unexpectedly committed");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "rollback-material'") == 1, "rollback lost the previous material source");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + "rollback-canonical'") == 0, "rollback left a canonical source");
    require(f.cover("rollback-material") == old_path, "rollback lost the previous cover");
    require(f.fileCount() == before, "failed merge leaked newly prepared cover files");
}

// 映射发现与歌曲摘要同时提交，任意调度顺序最终都只能保留规范来源。
void verifyConcurrentDiscovery(Fixture &f)
{
    for (int iteration = 0; iteration < 4; ++iteration) {
        const std::string material = "race-material-" + std::to_string(iteration), canonical = "race-canonical-" + std::to_string(iteration);
        auto song = f.song("race-song-" + std::to_string(iteration), material);
        song.cover_url.clear();
        model::MetadataWriteBatch first;
        first.songs = {song};
        first = use::buildMetadataWriteBatch(std::move(first));
        model::MetadataWriteBatch second;
        second.albums = {f.digital(canonical, material)};
        second = use::buildMetadataWriteBatch(std::move(second));
        std::latch ready(2);
        const auto run = [&](const model::MetadataWriteBatch &batch) {ready.arrive_and_wait();return f.writer->saveBatch(batch); };
        auto a = std::async(std::launch::async, run, std::cref(first));
        auto b = std::async(std::launch::async, run, std::cref(second));
        const auto ra = a.get(), rb = b.get();
        if (!ra.isSuccess() || !rb.isSuccess()) throw std::runtime_error("concurrent metadata write failed: " + ra.message() + " / " + rb.message());
        require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + f.prefix + material + "'") == 0, "concurrent discovery left a material source");
        require(f.scalar("SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + f.prefix + canonical + "'") == 1, "concurrent discovery lost a relation");
    }
}

// 同状态回写成功，缺失目标失败；未知总数不自动重排，明确重置后允许重试。
void verifySyncResults(Fixture &f)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        require(f.queue->updateDetailSyncStatus("migu", f.prefix + "canonical", 2, {}), "unchanged detail status was reported as failure");
        require(f.queue->updateSongsSyncStatus("migu", f.prefix + "canonical", 2, 1, {}), "unchanged song status was reported as failure");
    }
    require(!f.queue->updateDetailSyncStatus("migu", f.prefix + "missing", 2, {}), "missing detail target was accepted");
    require(!f.queue->updateSongsSyncStatus("migu", f.prefix + "missing", 2, 0, {}), "missing songs target was accepted");
    model::AlbumSourceMetadata unknown;
    unknown.source_album_id = f.prefix + "unknown-total";
    unknown.title = "未知总数";
    unknown.detail_sync_status = 2;
    unknown.songs_sync = model::AlbumSongSyncResult {4, 0, "album song completeness unknown: totalCount unavailable"};
    require(f.saver->saveAlbumSources({unknown}, 1), "unknown-total result failed");
    const auto contains = [&](const std::vector<model::AlbumSourceMetadata> &values) { return std::any_of(values.begin(), values.end(), [&](const auto &value) { return value.source_album_id == unknown.source_album_id; }); };
    require(!contains(f.queue->findPendingSongs("migu", 1000, 0)), "unknown total caused an automatic retry");
    require(f.queue->updateSongsSyncStatus("migu", unknown.source_album_id, 0, 0, {}), "explicit unknown-total reset failed");
    require(contains(f.queue->findPendingSongs("migu", 1000, 0)), "explicit reset did not allow a retry");
}

// 实测旧合唱格式必须在生产 parser/workflow/writer 中形成独立端点，整批一次提交。
void verifyDelimitedAlbumArtists(Fixture &f)
{
    model::AlbumSourceMetadata album;
    album.source_album_id = f.prefix + "duet-album";
    album.title = "合唱专辑事务验证";
    album.source_res_type = "0";
    require(f.saver->saveAlbumSources({album}, 1), "duet album discovery failed");
    const auto one = f.prefix + "duet-one", two = f.prefix + "duet-two", content = f.prefix + "duet-song";
    const std::string raw = "{\"resource\":[{\"albumId\":\"" + album.source_album_id + "\",\"resourceType\":\"2003\",\"title\":\"合唱专辑事务验证\",\"singerId\":\"" + one + "|" + two + "\",\"singer\":\"合唱歌手一|合唱歌手二\",\"totalCount\":\"1\",\"songItems\":[{\"contentId\":\"" + content + "\",\"songName\":\"合唱曲目\",\"singerId\":\"" + one + "|" + two + "\",\"singer\":\"合唱歌手一|合唱歌手二\",\"audioFormats\":[{\"formatType\":\"LQ\",\"resourceType\":\"2\",\"iformat\":\"fixture\"}]}]}]}";
    auto provider = std::make_shared<c::tests::MockMusicProvider>();
    model::ProviderResponse response;
    response.status_code = 200;
    response.body.assign(raw.begin(), raw.end());
    provider->mock_album_detail_response = response;
    auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
    auto dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
    use::AlbumDetailBackfillWorkflow workflow(provider, f.queue, dispatch, f.saver, nullptr, nullptr);
    require(workflow.crawlAlbumDetailAndSave(album), "delimited album transaction failed");
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id IN('" + one + "','" + two + "')") == 2, "duet singers were not split into independent sources");
    require(f.scalar("SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id='" + one + "|" + two + "'") == 0, "compound singer source was persisted");
    require(f.scalar("SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album.source_album_id + "'") == 2, "duet album artist relations were lost");
    require(f.scalar("SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_content_id='" + content + "'") == 2, "duet song artist relations were lost");
    require(f.scalar("SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + album.source_album_id + "' AND detail_sync_status=2 AND songs_sync_status=2 AND songs_sync_count=1") == 1, "duet metadata and completion were not committed together");
    require(f.scalar("SELECT COUNT(*) FROM song_source_audio_formats f JOIN song_source_refs s ON s.id=f.song_source_ref_id WHERE s.source_content_id='" + content + "' AND f.format_type='LQ'") == 1, "additional supplied audio format was lost");

    // 摘要重采只能补缺，入口归属和无序歌曲卡片不得覆盖已有详情署名及曲目位置。
    auto tracks = parser->parseSongSources(std::vector<char>(response.body));
    require(tracks.size() == 1, "duet track fixture missing");
    auto summaries = tracks;
    summaries.front().album_song_order = 0;
    require(f.saver->saveSongSources(summaries, 0), "empty Disc summary must remain a valid non-NULL value");
    tracks.front().album_disc_no = 1;
    tracks.front().album_disc_name = "1";
    tracks.front().album_track_no = 7;
    require(f.saver->saveSongSources(tracks, 0), "full track order discovery failed");
    tracks.front().album_disc_no = 2;
    tracks.front().album_disc_name = "Disc 2";
    tracks.front().album_track_no = 4;
    tracks.front().album_song_order = 0;
    require(f.saver->saveSongSources(tracks, 0), "song summary repeat failed");
    require(f.scalar("SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album.source_album_id + "' AND r.disc_no=1 AND r.disc_name='1' AND r.track_no=7 AND r.song_order=1") == 1, "summary replaced authoritative track position");
    model::SongSourceArtistRef entry;
    entry.source_artist_id = two;
    entry.is_seed = true;
    album.artist_refs = {entry};
    require(f.saver->saveAlbumSources({album}, 1), "album summary repeat failed");
    require(f.scalar("SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE a.source_album_id='" + album.source_album_id + "' AND s.source_singer_id='" + two + "' AND r.artist_order=2 AND r.is_primary=0 AND r.is_seed=1") == 1, "entry membership replaced authoritative artist credit");
}

} // namespace

int main()
{
    try {
        Fixture fixture;
        verifyAliasMerge(fixture);
        verifyMergeRollback(fixture);
        verifyConcurrentDiscovery(fixture);
        verifySyncResults(fixture);
        verifyDelimitedAlbumArtists(fixture);
        fixture.pool->cancel();
        std::cout << "PASS: canonical album identity, alias resources and relations, rollback, concurrent discovery, no-op status and unknown-total retry\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
