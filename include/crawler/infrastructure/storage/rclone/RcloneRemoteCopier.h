#pragma once

#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/storage/AsyncSongResourceRemoteCopier.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {

// 基于 rclone RC operations/copyurl 的远端复制 adapter。
// 线程安全：copyUrl 可被多个协程并发调用，阻塞轮询被隔离到 AsyncRuntime 的 blocking executor。
class RcloneRemoteCopier : public application::ports::AsyncSongResourceRemoteCopier {
public:
    RcloneRemoteCopier(
        std::shared_ptr<http::client::HttpClient> http_client,
        std::shared_ptr<application::ports::AsyncRuntime> runtime,
        RcloneConfig config
    );

    // 投递 copyurl 异步 job，轮询完成后通过 operations/stat 验证目标对象。
    application::async::Task<application::model::SongResourceRemoteCopyResult> copyUrl(application::model::SongResourceRemoteCopyRequest request) override;

private:
    application::model::SongResourceRemoteCopyResult copyUrlBlocking(application::model::SongResourceRemoteCopyRequest request);

    std::shared_ptr<http::client::HttpClient> http_client_;     // rclone RC HTTP client
    std::shared_ptr<application::ports::AsyncRuntime> runtime_; // 阻塞 job 轮询隔离 runtime
    RcloneConfig config_;                                       // rclone RC 运行配置
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
