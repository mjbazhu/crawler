#include "crawler/infrastructure/storage/filesystem/LocalLyricArtifactStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace fs = std::filesystem;
using Store = crawler::infrastructure::storage::filesystem::LocalLyricArtifactStore;

// 测试断言不依赖 assert。
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

// 测试目录只在系统临时目录下创建，析构前重新确认删除边界。
struct TemporaryRoot {
    fs::path path; // 当前测试独占的目录
    // 使用时间戳避免并行测试复用目录，已存在时直接拒绝使用。
    TemporaryRoot()
    {
        path = fs::weakly_canonical(fs::temp_directory_path()) / ("crawler-lyrics-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(fs::create_directory(path), "temporary directory must be exclusive");
    }
    // 只移除已验证父目录的测试根，不触碰运行数据。
    ~TemporaryRoot()
    {
        std::error_code error;
        const auto resolved = fs::weakly_canonical(path, error);
        if (!error && resolved.parent_path() == fs::weakly_canonical(fs::temp_directory_path()) && resolved.filename().string().starts_with("crawler-lyrics-test-")) fs::remove_all(path, error);
    }
};

// 测试文件写入完成后核对流状态。
void write(const fs::path &path, const std::string &content)
{
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    stream.close();
    require(!stream.fail(), "fixture write must succeed");
}

// 读取测试目录中的文件，不使用用户缓存或下载目录。
std::string read(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "published file should be readable");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// 两种产物始终使用固定文件名，内容用于验证整体更新。
std::vector<model::EncodedLyricArtifact> artifacts()
{
    return {{"word_json", "words.json", "application/json", "{}\n"}, {"enhanced_lrc", "words.enhanced.lrc", "text/plain", "[00:01.000]<00:01.000>甲<00:01.100>\n"}};
}

// 并发重试、来源和版本变化均维持单层路径，损坏的派生文件允许重新生成。
void testFixedPathsAndUpdates()
{
    TemporaryRoot root;
    const fs::path relative = "migu/songs/600908000006552646/lyrics";
    const auto directory = root.path / relative;
    fs::create_directories(directory);
    write(directory / "mrc.mrc", "source");
    write(directory / "lyric.lrc", "original lrc");
    write(directory / "trc.trc", "original trc");
    const Store store(root.path);
    const auto source = store.readSource((relative / "mrc.mrc").generic_string(), 1024);
    require(source.isSuccess() && source.data()->checksum.size() == 64, "source should have a full SHA-256");
    const model::LyricProcessingRequest request {7, (relative / "mrc.mrc").generic_string(), source.data()->checksum};
    const auto files = artifacts();
    auto publish = [&] { return store.publish(request, source.data()->checksum, "test-v1", 1, files); };
    auto first = std::async(std::launch::async, publish);
    auto second = std::async(std::launch::async, publish);
    const auto one = first.get();
    const auto two = second.get();
    require(one.isSuccess() && two.isSuccess(), "concurrent identical publishing should succeed");
    require(one.data()->files[0].local_path == (relative / "words.json").generic_string(), "JSON should be next to the original lyrics");
    require(one.data()->files[1].local_path == (relative / "words.enhanced.lrc").generic_string(), "enhanced LRC should use a stable single-level path");
    require(one.data()->files[0].local_path == two.data()->files[0].local_path, "retry paths must be identical");
    const auto json = directory / "words.json";
    const auto modified = fs::last_write_time(json);
    require(!fs::exists(directory / "words.manifest.json"), "publication should not create a manifest");
    require(one.data()->source_checksum == source.data()->checksum && one.data()->processor_version == "test-v1" && one.data()->schema_version == 1, "source and version metadata should be returned for database indexing");
    require(one.data()->files[0].size_bytes == files[0].content.size() && one.data()->files[0].checksum.size() == 64, "actual size and checksum should be returned for database indexing");
    write(directory / "words.manifest.json", "legacy metadata");
    require(publish().isSuccess() && fs::last_write_time(json) == modified, "identical retry should not rewrite existing files");
    require(!fs::exists(directory / "words.manifest.json"), "reprocessing should remove the obsolete manifest");
    auto upgraded = files;
    upgraded[0].content = "{\"schema_version\":2}\n";
    upgraded[1].content = "[00:02.000]乙\n";
    const auto next = store.publish(request, source.data()->checksum, "test-v2", 2, upgraded);
    require(next.isSuccess() && next.data()->files[0].local_path == one.data()->files[0].local_path, "new versions should reuse fixed paths");
    require(read(json) == upgraded[0].content && read(directory / "words.enhanced.lrc") == upgraded[1].content, "both formats should update together");
    require(next.data()->schema_version == 2 && next.data()->processor_version == "test-v2", "new versions should enter database metadata");
    write(json, "corrupted");
    require(store.publish(request, source.data()->checksum, "test-v2", 2, upgraded).isSuccess() && read(json) == upgraded[0].content, "retry should repair a damaged derived file");
    write(directory / "mrc.mrc", "new source");
    const auto stale = store.publish(request, source.data()->checksum, "test-v2", 2, upgraded);
    require(!stale.isSuccess() && stale.message() == "lyric_source_changed", "stale decoded data must not replace current output");
    const auto current = store.readSource(request.local_path, 1024);
    const model::LyricProcessingRequest changed {7, request.local_path, current.data()->checksum};
    require(store.publish(changed, current.data()->checksum, "test-v2", 2, upgraded).isSuccess(), "changed source should publish to the same fixed paths");
    require(read(directory / "mrc.mrc") == "new source" && read(directory / "lyric.lrc") == "original lrc" && read(directory / "trc.trc") == "original trc", "publishing must preserve all raw lyrics");
    for (const auto &entry : fs::directory_iterator(directory)) require(!entry.is_directory(), "no version, hash or staging directories should remain");
}

// 路径越界、覆盖来源或覆盖原始 LRC 必须在文件替换前失败。
void testBoundaries()
{
    TemporaryRoot root;
    const Store store(root.path);
    write(root.path / "raw.mrc", "source");
    write(root.path / "lyric.lrc", "original lrc");
    const auto source = store.readSource("raw.mrc", 1024);
    const model::LyricProcessingRequest request {7, "raw.mrc", source.data()->checksum};
    require(!store.readSource("../outside.mrc", 1024).isSuccess(), "parent traversal must fail");
    require(!store.readSource((root.path / "raw.mrc").string(), 1024).isSuccess(), "absolute input path must fail");
    require(!store.readSource("raw.mrc", 1).isSuccess(), "size limit should apply before reading");
    require(!store.readSource("missing.mrc", 1024).isSuccess(), "missing input must fail");
    auto unsafe = artifacts();
    unsafe[0].filename = "../escape.json";
    require(!store.publish(request, source.data()->checksum, "test-v1", 1, unsafe).isSuccess(), "unsafe filename must fail");
    unsafe = artifacts();
    unsafe[1].filename = "lyric.lrc";
    require(!store.publish(request, source.data()->checksum, "test-v1", 1, unsafe).isSuccess(), "original LRC filename must not be used for derived output");
    require(read(root.path / "lyric.lrc") == "original lrc", "invalid output name must not alter the source LRC");
    const auto files = artifacts();
    require(!store.publish(request, source.data()->checksum, "../version", 1, files).isSuccess(), "invalid version metadata should fail");
    require(store.publish(request, source.data()->checksum, "test-v1", 1, files).isSuccess(), "sources directly in the storage root should also work");
    write(root.path / "words.manifest.json", "raw source");
    const auto manifest_source = store.readSource("words.manifest.json", 1024);
    require(!store.publish({7, "words.manifest.json", manifest_source.data()->checksum}, manifest_source.data()->checksum, "test-v1", 1, files).isSuccess(), "manifest cannot replace the raw input");
    require(read(root.path / "words.manifest.json") == "raw source", "rejected publication must preserve the raw source");
}

#ifdef _WIN32
// 拒绝删除共享，模拟第二个目标被其他程序占用，验证首个文件已替换后的回滚。
class LockedFile {
public:
    // 锁只在当前测试作用域内持有，不访问用户文件。
    explicit LockedFile(const fs::path &path) : handle_(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
        require(handle_ != INVALID_HANDLE_VALUE, "test file lock should be acquired");
    }
    // 释放测试持有的 Windows 文件句柄。
    ~LockedFile()
    {
        CloseHandle(handle_);
    }
    // 文件锁独占所有权，不允许复制。
    LockedFile(const LockedFile &) = delete;
    // 文件锁独占所有权，不允许赋值复制。
    LockedFile &operator=(const LockedFile &) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE; // 当前测试拥有的文件句柄
};

// 中途替换失败必须恢复两个文件，解锁后可以正常重试。
void testReplacementRollback()
{
    TemporaryRoot root;
    const Store store(root.path);
    write(root.path / "raw.mrc", "source");
    const auto source = store.readSource("raw.mrc", 1024);
    const model::LyricProcessingRequest request {7, "raw.mrc", source.data()->checksum};
    const auto old = artifacts();
    require(store.publish(request, source.data()->checksum, "test-v1", 1, old).isSuccess(), "initial publication should succeed");
    write(root.path / "words.manifest.json", "legacy metadata");
    {
        LockedFile locked(root.path / "words.manifest.json");
        const auto failed = store.publish(request, source.data()->checksum, "test-v1", 1, old);
        require(!failed.isSuccess(), "locked obsolete manifest should report cleanup failure");
        require(read(root.path / "words.json") == old[0].content && read(root.path / "words.enhanced.lrc") == old[1].content, "cleanup failure must preserve the existing outputs");
    }
    require(store.publish(request, source.data()->checksum, "test-v1", 1, old).isSuccess() && !fs::exists(root.path / "words.manifest.json"), "unlocked obsolete manifest should be removed on retry");
    auto next = old;
    next[0].content = "{\"updated\":true}\n";
    next[1].content = "[00:02.000]乙\n";
    {
        LockedFile locked(root.path / "words.enhanced.lrc");
        const auto failed = store.publish(request, source.data()->checksum, "test-v2", 1, next);
        require(!failed.isSuccess() && failed.message() == "lyric_artifact_publish_failed", "locked second file should fail publication");
        require(read(root.path / "words.json") == old[0].content && read(root.path / "words.enhanced.lrc") == old[1].content, "partial replacement must restore both old files");
        require(!fs::exists(root.path / "words.manifest.json"), "failed publication must not create a manifest");
    }
    require(store.publish(request, source.data()->checksum, "test-v2", 1, next).isSuccess(), "unlocked retry should publish successfully");
    for (const auto &entry : fs::directory_iterator(root.path)) require(!entry.is_directory(), "rollback and successful retry should clean their own staging directories");
}
#endif

} // namespace

// 执行真实文件系统回归，不依赖用户缓存或数据库。
int main()
{
    try {
        testFixedPathsAndUpdates();
        testBoundaries();
#ifdef _WIN32
        testReplacementRollback();
#endif
        std::cout << "LocalLyricArtifactStoreTest passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "LocalLyricArtifactStoreTest failed: " << error.what() << '\n';
        return 1;
    }
}
