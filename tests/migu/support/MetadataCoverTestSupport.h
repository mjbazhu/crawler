#pragma once

#include "crawler/application/ports/persistence/MetadataCoverQueue.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::tests::cover {
namespace model = application::model;
namespace ports = application::ports;
namespace http = infrastructure::http::client;
namespace files = infrastructure::storage::filesystem;
template <typename T>
using Result = application::result::ServiceResult<T>;

// Debug/Release 都执行的断言。
inline void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 有完整 PNG 结构的单像素样本，不以文件名判断格式。
inline std::vector<char> png()
{
    const std::string hex = "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000b49444154789c636000020000050001a5f645400000000049454e44ae426082";
    std::vector<char> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) bytes.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return bytes;
}

// 本测试独占的临时目录，析构只清理自己创建的路径。
struct TempRoot {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("crawler_cover_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); // 独占目录
    TempRoot()
    {
        std::filesystem::create_directories(path);
    } // 创建临时存储根
    ~TempRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    } // 清理本次文件
};

// 可定向返回状态和图片的 HTTP fixture；不执行网络请求。
class Http final : public http::HttpClient {
public:
    int calls = 0;                                                               // 请求计数
    int status = 200;                                                            // 默认 HTTP 状态
    std::vector<char> body = png();                                              // 默认完整图片
    std::function<void()> on_get;                                                // 在下载中触发竞态或取消
    std::function<std::optional<http::HttpResponse>(std::string_view)> response; // 特定地址响应
    std::vector<std::string> last_headers;                                       // 检查跨域跳转头策略
    // 默认走 HttpClient 的流式兼容实现，直接验证回调行为。
    std::optional<http::HttpResponse> get(std::string_view url, const std::vector<std::string> &headers = {}) override
    {
        ++calls;
        last_headers = headers;
        if (on_get) on_get();
        if (response) return response(url);
        return http::HttpResponse {status, body, "image/png", {}};
    }
    // 本专项不调用 POST。
    std::optional<http::HttpResponse> post(std::string_view, std::string_view, const std::vector<std::string> & = {}) override
    {
        return std::nullopt;
    }
    // 满足客户端协程接口，测试仍使用同步端口。
    AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) override
    {
        co_return get(url, headers);
    }
    // 本专项不调用协程 POST。
    AwaitableResponse coPost(std::string, std::string, std::vector<std::string> = {}) override
    {
        co_return std::nullopt;
    }
};

// 线程安全的条件队列 fixture，真实文件由 FileStorage 处理。
class Queue final : public ports::MetadataCoverQueue {
public:
    using Key = std::pair<model::CoverOwner, std::uint64_t>;
    std::map<Key, model::MetadataCoverRecord> rows;                          // 测试数据，运行前填充
    std::mutex mutex;                                                        // 保护条件更新
    bool uncertain = false;                                                  // 模拟成功提交响应不确定
    bool committed_before_disconnect = false;                                // 模拟实际已提交
    std::function<void(const model::MetadataResourceFile &)> before_success; // 检查回写前文件存在

    // 按类型和完整过滤查询。
    Result<model::MetadataCoverPage> list(const model::MetadataCoverQuery &query) override
    {
        std::lock_guard lock(mutex);
        model::MetadataCoverPage page;
        page.page = query.page;
        page.page_size = query.page_size;
        for (const auto &[key, row] : rows) {
            const auto contains = [](const auto &values, const auto &value) { return values.empty() || std::find(values.begin(), values.end(), value) != values.end(); };
            if (row.owner != query.owner || row.source != query.source || !contains(query.resource_ids, row.resource_id) || !contains(query.source_ref_ids, row.source_ref_id) || !contains(query.statuses, row.file.status)) continue;
            if (page.total++ >= static_cast<std::uint64_t>(query.page - 1) * query.page_size && page.items.size() < static_cast<std::size_t>(query.page_size)) page.items.push_back(row);
        }
        return Result<model::MetadataCoverPage>::success(std::move(page));
    }
    // 查找与空命中分别返回。
    Result<std::optional<model::MetadataCoverRecord>> find(model::CoverOwner owner, std::uint64_t id) override
    {
        std::lock_guard lock(mutex);
        const auto found = rows.find({owner, id});
        return Result<std::optional<model::MetadataCoverRecord>>::success(found == rows.end() ? std::nullopt : std::optional(found->second));
    }
    // 比较代次、URL 和状态，复现真实条件更新。
    Result<std::optional<model::MetadataCoverRecord>> claim(const model::MetadataCoverRecord &target, bool refresh, const std::string &batch) override
    {
        std::lock_guard lock(mutex);
        auto &row = rows.at({target.owner, target.resource_id});
        if (row.claim_generation != target.claim_generation || row.remote_url != target.remote_url || row.file.status == 1 || (row.file.status == 2 && !refresh)) return Result<std::optional<model::MetadataCoverRecord>>::success(std::nullopt);
        ++row.claim_generation;
        row.file.status = 1;
        row.running_batch_id = batch;
        return Result<std::optional<model::MetadataCoverRecord>>::success(row);
    }
    // 验证回写期间新旧文件都存在，随后模拟条件提交。
    Result<bool> succeed(const model::MetadataCoverRecord &target, const model::MetadataResourceFile &file) override
    {
        if (before_success) before_success(file);
        std::lock_guard lock(mutex);
        auto &row = rows.at({target.owner, target.resource_id});
        if (row.claim_generation != target.claim_generation || row.file.status != 1 || row.remote_url != target.remote_url) return Result<bool>::success(false);
        if (!uncertain || committed_before_disconnect) row.file = file;
        if (uncertain) return Result<bool>::failure(application::result::ServiceErrorType::CommitUncertain, "fixture uncertain");
        return Result<bool>::success(true);
    }
    // 失败不覆盖旧文件路径或 checksum。
    Result<bool> fail(const model::MetadataCoverRecord &target, const model::MetadataResourceFile &error) override
    {
        std::lock_guard lock(mutex);
        auto &row = rows.at({target.owner, target.resource_id});
        if (row.claim_generation != target.claim_generation || row.file.status != 1) return Result<bool>::success(false);
        row.file.status = row.file.local_path.empty() ? 3 : 2;
        row.file.error_category = error.error_category;
        row.file.result_code = error.result_code;
        row.file.last_error = error.last_error;
        return Result<bool>::success(true);
    }
    // 由测试显式模拟超时，不使用真实时钟等待。
    Result<int> recover(const model::MetadataCoverQuery &query, int) override
    {
        std::lock_guard lock(mutex);
        int recovered = 0;
        for (auto &[key, row] : rows)
            if (row.owner == query.owner && row.file.status == 1) {
                ++row.claim_generation;
                row.file.status = row.file.local_path.empty() ? 3 : 2;
                ++recovered;
            }
        return Result<int>::success(recovered);
    }
};

// 创建两类资源可共用相同数字 ID 的 fixture。
inline model::MetadataCoverRecord target(model::CoverOwner owner, std::uint64_t id = 1)
{
    model::MetadataCoverRecord row;
    row.owner = owner;
    row.resource_id = id;
    row.source_ref_id = id + 10;
    row.source_id = std::string(model::coverOwnerName(owner)) + '-' + std::to_string(id);
    row.remote_url = "https://example.test/image.png";
    return row;
}

} // namespace orchestrator::modules::crawler::tests::cover
