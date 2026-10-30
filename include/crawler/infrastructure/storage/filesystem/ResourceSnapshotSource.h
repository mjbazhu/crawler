#pragma once
#include "crawler/application/ports/persistence/ResourceTransferSources.h"
#include "crawler/application/ports/provider/ResourceTransferSource.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "crawler/infrastructure/storage/filesystem/ResourceArtifactProducer.h"
#include <array>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
class ResourceTokenRefresh;
// 来源能力按职责注入，不装配正式本地回执写入。
struct ResourceSnapshotDependencies {
    std::shared_ptr<application::ports::ResourceTransferSources> descriptions;                                     // 只读入库描述
    std::shared_ptr<application::ports::SongResourceProvider> songs;                                               // 合法临时 URL
    std::shared_ptr<application::ports::SongResourceParser> parser;                                                // listen 响应
    std::shared_ptr<application::ports::SongResourceContentFetcher> content;                                       // 有界流式下载
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> token_refresher;                                   // 现有认证刷新
    std::shared_ptr<application::ports::MiguPacmtokenStore> token_store;                                           // 线程安全 token
    std::shared_ptr<ResourceTokenRefresh> token_refresh;                                                           // 本端跨项协调初次/周期刷新
    std::shared_ptr<http::client::HttpClient> covers;                                                              // 受控封面下载
    std::vector<std::string> cover_headers;                                                                        // 不可变来源 headers
    std::shared_ptr<FileStorage> artifacts;                                                                        // 读取既有产物，不创建正式根
    std::shared_ptr<ResourceArtifactProducer> producer;                                                            // 三端共享的受控生成缓存和登记
    std::function<ResourceSnapshotDependencies(const application::model::ResourceTransferContext &)> bind_context; // 组合根提供本次来源 HTTP 预算，不共享可变上下文
    std::optional<application::model::ResourceTransferContext> execution_context;                                  // 生产分片只读缓存取消状态
};

// 线程安全来源 adapter；三端独占临时区、每端空间配额及不可变租约。
class ResourceSnapshotSource final : public application::ports::ResourceTransferSource {
public:
    // 不创建任何目录；首次真实获取才使用相应 staging 根。
    ResourceSnapshotSource(ResourceSnapshotDependencies dependencies, std::array<std::filesystem::path, 3> staging_roots, std::uint64_t per_target_quota_bytes = 8ULL * 1024 * 1024 * 1024);
    // 先描述再获取；不等待其他目标，不把临时文件确认为正式 local。
    application::result::ServiceResult<std::shared_ptr<const application::model::ResourceContentSnapshot>> acquire(const application::model::ResourceTransferJob &job, const application::model::ResourceTransferContext &context) override;

private:
    ResourceSnapshotDependencies dependencies_;                           // 创建后不变
    std::array<std::filesystem::path, 3> roots_;                          // 三端独立显式根
    std::shared_ptr<std::array<std::atomic<std::uint64_t>, 3>> reserved_; // 跟随租约生命周期释放
    std::uint64_t quota_;                                                 // 每端最大保留字节预算
};
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
