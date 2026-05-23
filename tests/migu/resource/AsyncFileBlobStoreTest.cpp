#include "crawler/application/use_cases/AsyncResourcePortAdapters.h"
#include "crawler/infrastructure/execution/asio/AsioAsyncRuntime.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace use_cases = crawler::application::use_cases;
namespace asio_runtime = crawler::infrastructure::execution::asio;
namespace filesystem_storage = crawler::infrastructure::storage::filesystem;

namespace {

// 测试断言失败时抛出明确错误，避免依赖 assert 宏。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 每个阻塞任务使用独立线程，并记录提交次数。
class CountingTaskExecutor : public ports::TaskExecutor {
public:
    ~CountingTaskExecutor() override
    {
        for (auto &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void submit(std::function<void()> task) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++submitted_;
        threads_.emplace_back(std::move(task));
    }

    int submitted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return submitted_;
    }

private:
    mutable std::mutex mutex_;           // 保护提交记录
    int submitted_ = 0;                  // 已提交任务数
    std::vector<std::thread> threads_;   // 阻塞任务线程
};

// 异步写入完整对象并在 commit 前检查最终路径不可见。
app_async::Task<model::SongResourceBlobSaveResult> writeAndCommit(
    ports::AsyncSongResourceBlobStore &store,
    const std::filesystem::path &root,
    std::string relative_path
)
{
    model::SongResourceBlobStreamSaveRequest request;
    request.relative_path_hint = relative_path;
    request.content_type = "audio/flac";
    auto session = co_await store.open(request);
    if (!session) {
        model::SongResourceBlobSaveResult result;
        result.error = "open_failed";
        co_return result;
    }

    const std::string first = "ab";
    auto first_write = co_await session->write(std::span<const char>(first.data(), first.size()));
    if (!first_write.ok) {
        model::SongResourceBlobSaveResult result;
        result.error = first_write.error;
        co_return result;
    }
    if (std::filesystem::exists(root / relative_path)) {
        model::SongResourceBlobSaveResult result;
        result.error = "final_path_visible_before_commit";
        co_return result;
    }

    const std::string second = "cd";
    auto second_write = co_await session->write(std::span<const char>(second.data(), second.size()));
    if (!second_write.ok) {
        model::SongResourceBlobSaveResult result;
        result.error = second_write.error;
        co_return result;
    }
    co_return co_await session->commit();
}

// 异步写入后放弃，验证最终路径不暴露。
app_async::Task<void> writeAndAbort(ports::AsyncSongResourceBlobStore &store)
{
    model::SongResourceBlobStreamSaveRequest request;
    request.relative_path_hint = "migu/songs/600902000006889266/audio/abort.flac";
    request.content_type = "audio/flac";
    auto session = co_await store.open(request);
    if (!session) {
        co_return;
    }
    const std::string data = "partial";
    (void)co_await session->write(std::span<const char>(data.data(), data.size()));
    co_await session->abort();
}

void testAsyncFileBlobStoreCommitAndAbort()
{
    const auto root = std::filesystem::current_path() / "build" / "async-file-blob-store-test";
    std::filesystem::remove_all(root);

    boost::asio::io_context io_context;
    auto guard = boost::asio::make_work_guard(io_context);
    auto blocking_executor = std::make_shared<CountingTaskExecutor>();
    auto runtime = std::make_shared<asio_runtime::AsioAsyncRuntime>(io_context.get_executor(), blocking_executor);
    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    auto storage = std::make_shared<filesystem_storage::FileStorage>(root);
    use_cases::BlockingAsyncSongResourceBlobStore async_store(storage, runtime);

    const std::string relative_path = "migu/songs/600902000006889266/audio/ZQ24.flac";
    const auto result = runtime->runForTest(writeAndCommit(async_store, root, relative_path));
    require(result.ok, "async file blob commit should succeed");
    require(result.local_path == relative_path, "async file blob should preserve relative path");
    require(result.size_bytes == 4, "async file blob should record size");
    require(result.checksum == "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589", "async file blob should record sha256");
    require(std::filesystem::exists(root / relative_path), "final file should exist after commit");
    {
        std::ifstream saved_file(root / relative_path, std::ios::binary);
        const std::string saved_text((std::istreambuf_iterator<char>(saved_file)), std::istreambuf_iterator<char>());
        require(saved_text == "abcd", "final file should contain all chunks");
    }

    runtime->runForTest(writeAndAbort(async_store));
    require(!std::filesystem::exists(root / "migu/songs/600902000006889266/audio/abort.flac"), "aborted async file should not expose final path");
    require(blocking_executor->submitted() >= 5, "open/write/commit/abort should be dispatched to blocking executor");

    guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    try {
        testAsyncFileBlobStoreCommitAndAbort();
        std::cout << "AsyncFileBlobStoreTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "AsyncFileBlobStoreTest failed: " << e.what() << std::endl;
        return 1;
    }
}
