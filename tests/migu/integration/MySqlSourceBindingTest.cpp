#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace storage = c::infrastructure::storage::filesystem;
namespace support = c::tests::migu::support;

namespace {

// 断言始终执行，不能依赖构建配置中的 assert 开关。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 使用真实事务核验地址保存，禁止连接正式库或触发外部下载。
class Fixture {
public:
    Fixture()
    {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_"), "refusing non-test database");
        prefix = "binding_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_";
        pool = support::makeInitializedPool();
        writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        directory = std::filesystem::temp_directory_path() / config.database / prefix;
        files = std::make_shared<storage::FileStorage>(directory.string());
        saver = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, {}});
    }

    // 独立只读 SQL 核验最终结果，不复用 writer 的内存状态。
    std::uint64_t scalar(const std::string &sql)
    {
        const auto result = support::runOnExecutor(pool->executor(), pool->coExecute(sql), "binding verification");
        return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
    }

    // 返回当前封面地址，以便验证详情与摘要的更新优先级。
    std::string cover()
    {
        const auto result = support::runOnExecutor(pool->executor(), pool->coExecute("SELECT r.remote_url FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + prefix + "cover'"), "cover verification");
        return mysql::MySqlValueReader::stringAt(result.rows().front(), 0);
    }

    // 文件数保持不变用于排除未采用图片或旧图片泄漏。
    std::size_t fileCount() const
    {
        std::size_t count = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
            if (entry.is_regular_file()) ++count;
        return count;
    }

    // 构造共享 songId、不同 contentId 的两个真实入口形态。
    model::SongSourceMetadata song(const std::string &suffix) const
    {
        model::SongSourceMetadata value;
        value.source = "migu";
        value.title = "来源绑定测试";
        value.source_song_id = prefix + "song";
        value.source_content_id = prefix + suffix;
        value.copyright_id = prefix + suffix;
        value.source_title_pinyin = suffix;
        value.source_duration_seconds = 220;
        value.source_resource_type = "2";
        value.source_album_id = prefix + "album-" + suffix;
        value.source_album_name = "入口专辑";
        value.audio_formats = {{"PQ", "2", suffix, suffix, 10, 12}};
        return value;
    }

    // 每次输入都先经过生产批次构建，包含歌曲推导的独立专辑端点。
    auto save(const model::SongSourceMetadata &song)
    {
        model::MetadataWriteBatch batch;
        batch.songs = {song};
        return writer->saveBatch(use::buildMetadataWriteBatch(std::move(batch)));
    }

    std::string prefix;                                      // 本次运行唯一的测试来源前缀
    std::filesystem::path directory;                         // 独立测试文件根目录
    std::shared_ptr<mysql::MySqlConnectionPool> pool;        // 真实 MySQL 连接池
    std::shared_ptr<mysql::MySqlMetadataBatchWriter> writer; // 生产事务适配器
    std::shared_ptr<storage::FileStorage> files;             // 真实文件适配器
    std::shared_ptr<use::MetadataPersistenceService> saver;  // 生产封面补偿事务
};

// 已绑定内容不能被其他入口切换，关系仍能补齐，音质和资源任务必须保持一致。
void verifyStableBinding(Fixture &f)
{
    const auto first = f.song("primary");
    require(f.save(first).isSuccess(), "primary binding failed");
    auto alternate = f.song("alternate");
    alternate.source_duration_seconds = 999;
    alternate.source_resource_type = "E";
    alternate.audio_formats.push_back({"SQ", "E", "wrong", "wrong", 900, 900});
    alternate.cover_url = "https://example.test/alternate";
    const auto check = [&](const auto &result) {
        require(result.isSuccess() && result.data() && result.data()->saved_song_sources.size() == 1, "alternate transaction failed");
        const auto &identity = result.data()->saved_song_sources.front();
        require(identity.song_id == first.source_song_id && identity.content_id == first.source_content_id, "writer returned input identity instead of persisted binding");
    };
    check(f.save(alternate));
    auto a = std::async(std::launch::async, [&] { return f.save(alternate); });
    auto b = std::async(std::launch::async, [&] { return f.save(first); });
    check(a.get());
    check(b.get());
    const auto predicate = "s.source_song_id='" + first.source_song_id + "'";
    require(f.scalar("SELECT COUNT(*) FROM song_source_refs s WHERE " + predicate + " AND source_content_id='" + first.source_content_id + "' AND copyright_id='" + first.copyright_id + "' AND source_duration_seconds=220 AND source_resource_type='2' AND source_title_pinyin='primary'") == 1, "alternate entry changed primary content metadata");
    require(f.scalar("SELECT COUNT(*) FROM song_source_audio_formats f JOIN song_source_refs s ON s.id=f.song_source_ref_id WHERE " + predicate + " AND f.format_type='PQ' AND f.iformat='primary'") == 1, "alternate entry replaced current audio parameters");
    require(f.scalar("SELECT COUNT(*) FROM song_source_audio_formats f JOIN song_source_refs s ON s.id=f.song_source_ref_id WHERE " + predicate) == 1, "alternate content created an incompatible audio format");
    require(f.scalar("SELECT COUNT(*) FROM album_song_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE " + predicate) == 2, "alternate album relation was lost");
    require(f.scalar("SELECT COUNT(*) FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE " + predicate + " AND r.resource_kind='cover'") == 0, "alternate content created an unrelated resource task");
}

// 尚缺一端身份时允许补齐，而已有完整身份必须保持稳定。
void verifyIdentityCompletion(Fixture &f)
{
    auto partial = f.song("partial-content");
    partial.source_song_id = f.prefix + "partial-song";
    auto complete = partial;
    partial.source_content_id.clear();
    require(f.save(partial).isSuccess() && f.save(complete).isSuccess(), "missing content identity could not be filled");
    partial = f.song("only-content");
    partial.source_song_id.clear();
    complete = partial;
    complete.source_song_id = f.prefix + "late-song";
    require(f.save(partial).isSuccess() && f.save(complete).isSuccess(), "missing song identity could not be filled");
    const auto result = f.save(complete);
    require(result.data() && result.data()->saved_song_sources.front().song_id == complete.source_song_id, "completed identity was not returned");
}

// 摘要不覆盖详情地址；元数据刷新只更新地址，不下载文件或改变资源状态。
void verifyCoverAuthority(Fixture &f)
{
    model::AlbumSourceMetadata album;
    album.source_album_id = f.prefix + "cover";
    album.title = "封面优先级测试";
    album.source_album_kind = 1;
    album.detail_sync_status = 2;
    album.cover_url = "https://example.test/2";
    require(f.saver->saveAlbumSources({album}, 0), "detail cover URL seed failed");
    const auto original = f.cover();
    auto summary = album;
    summary.detail_sync_status = 0;
    summary.cover_url = "https://example.test/1";
    require(f.saver->saveAlbumSources({summary}, 0), "album summary failed");
    require(f.cover() == original, "summary replaced the detail cover URL");
    album.cover_url.clear();
    require(f.saver->saveAlbumSources({album}, 0) && f.cover() == original, "empty response cleared a known URL");
    album.cover_url = "https://example.test/1";
    require(f.saver->saveAlbumSources({album}, 0) && f.cover() == album.cover_url, "detail did not update the cover URL");
    require(f.scalar("SELECT COUNT(*) FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album.source_album_id + "' AND r.status=0 AND r.local_path IS NULL AND r.fetched_at=0") == 1, "URL registration was marked downloaded");
    require(f.fileCount() == 0, "metadata ingestion created binary files");
}

} // namespace

int main()
{
    try {
        Fixture fixture;
        verifyStableBinding(fixture);
        verifyIdentityCompletion(fixture);
        verifyCoverAuthority(fixture);
        fixture.pool->cancel();
        std::cout << "PASS: stable source binding, identity completion, alternate relations and detail cover authority\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
