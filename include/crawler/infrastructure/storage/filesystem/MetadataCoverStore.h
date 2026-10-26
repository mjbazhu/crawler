#pragma once

#include "crawler/application/ports/storage/MetadataCoverStore.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 线程安全的本地封面 adapter；共享 HTTP 客户端和文件存储，单次文件路径互不覆盖。
class MetadataCoverStore final : public application::ports::MetadataCoverStore {
public:
    // 保存线程安全依赖，headers 由组合根按音乐源策略提供。
    MetadataCoverStore(std::shared_ptr<http::client::HttpClient> client, std::shared_ptr<FileStorage> files, std::vector<std::string> headers);

    // 限制单个封面为 8 MiB、15 秒，并校验响应确实为支持的图片。
    application::model::MetadataResourceFile save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url) override;

    // 只删除本 adapter 返回的模块内相对路径。
    bool discard(std::string_view local_path) override;

private:
    std::shared_ptr<http::client::HttpClient> client_; // 线程安全 HTTP 客户端
    std::shared_ptr<FileStorage> files_;               // 原子文件保存器
    std::vector<std::string> headers_;                 // 不可变请求头策略
    std::atomic<std::uint64_t> sequence_ {0};          // 隔离并发采集文件名
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
