#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/application/ports/storage/MetadataCoverStore.h"
#include "crawler/application/ports/storage/MetadataCoverReader.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 线程安全的本地封面 adapter；共享 HTTP 客户端和文件存储，单次文件路径互不覆盖。
class MetadataCoverStore final : public application::ports::MetadataCoverStore, public application::ports::MetadataCoverReader {
public:
    // 保存线程安全依赖，headers 由组合根按音乐源策略提供。
    MetadataCoverStore(std::shared_ptr<http::client::HttpClient> client, std::shared_ptr<FileStorage> files, std::vector<std::string> headers);

    // 限制单个封面为 8 MiB，默认 15 秒，校验响应为支持的完整图片结构。
    application::model::MetadataResourceFile save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url) override;

    // 独立下载携带代次、取消及旧文件上下文。
    application::model::MetadataResourceFile save(application::model::MetadataCoverSaveRequest request) override;

    // 有界检查已提交文件，使用规范路径限制在存储根内。
    bool available(const application::model::MetadataResourceFile &file) const override;
    // 读取并验证实际图片内容与数据库回执。
    std::optional<application::model::MetadataCoverContent> read(const application::model::MetadataResourceFile &file) const override;

    // 成功事务解除会话补偿；不确定事务保留本地日志。
    void accept(std::string_view local_path) override;
    void defer(std::string_view local_path) override;

    // 只删除本 adapter 返回的模块内相对路径。
    bool discard(std::string_view local_path) override;

private:
    std::shared_ptr<http::client::HttpClient> client_; // 线程安全 HTTP 客户端
    std::shared_ptr<FileStorage> files_;               // 原子文件保存器
    std::vector<std::string> headers_;                 // 不可变请求头策略
    // 取出会话时只在短临界区修改映射，文件 I/O 在锁外执行。
    std::unique_ptr<application::ports::SongResourceBlobWriteSession> takeSession(std::string_view local_path);
    std::mutex sessions_mutex_;                                                                         // 保护未确认的独占文件会话
    std::map<std::string, std::unique_ptr<application::ports::SongResourceBlobWriteSession>> sessions_; // 事务提交前的封面文件
    std::atomic<std::uint64_t> sequence_ {0};                                                           // 隔离并发采集文件名
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
