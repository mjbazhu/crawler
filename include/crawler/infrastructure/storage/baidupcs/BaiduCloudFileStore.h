#pragma once

#include "crawler/application/model/CloudFile.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoConfig.h"

#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {

// 共用文件上传 port 的百度实现；线程安全，每次操作独占快照与子进程。
class BaiduCloudFileStore final : public application::ports::CloudFileStore {
public:
    // 复用现有 uploader，runner 可替换以验证真实进程边界。
    BaiduCloudFileStore(BaiduPcsGoConfig config, execution::process::CommandRunner runner = {});
    // 返回既有百度后端标识。
    std::string backend() const override;
    // 返回规范化的绝对云端前缀。
    std::string prefix() const override;
    // 上传已提交字节并回读 SHA-256 核验，不以退出码代替内容确认。
    application::model::CloudFileResult upload(application::model::CloudFileUploadRequest request) override;

private:
    BaiduPcsGoConfig config_;                  // 不可变后端设置
    execution::process::CommandRunner runner_; // 线程安全进程执行边界
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
