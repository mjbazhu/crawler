#pragma once

#include "crawler/application/model/CloudFile.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {

// Google 等已配置 remote 的本地文件上传 adapter；线程安全，无共享任务状态。
class RcloneCloudFileStore final : public application::ports::CloudFileStore {
public:
    // 复用 RC 地址、鉴权和 remote 配置，HTTP 客户端由组合根装配。
    RcloneCloudFileStore(std::shared_ptr<http::client::HttpClient> client, RcloneConfig config);
    // 与既有存储后端标识一致。
    std::string backend() const override;
    // 稳定完整 remote 前缀，例如 gcloud:Media/。
    std::string prefix() const override;
    // multipart 上传同一字节快照，并用远端大小和完整摘要核验。
    application::model::CloudFileResult upload(application::model::CloudFileUploadRequest request) override;

private:
    std::shared_ptr<http::client::HttpClient> client_; // 线程安全 RC 客户端
    RcloneConfig config_;                              // 不可变 RC 设置
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
