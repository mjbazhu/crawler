#pragma once

#include "crawler/application/ports/storage/SongResourceBatchCloudUploader.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoConfig.h"

#include <functional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {

// BaiduPCS-Go 批量上传 adapter；线程安全：无共享可变状态，uploadBatch 可并发调用。
class BaiduPcsGoBatchUploader : public application::ports::SongResourceBatchCloudUploader {
public:
    using CommandRunner = std::function<int(const std::string &command)>;

    explicit BaiduPcsGoBatchUploader(BaiduPcsGoConfig config, CommandRunner command_runner = {});

    // 将本地批次资源按相对路径 staging 后，一次性上传到 cloud_root。
    application::model::SongResourceBatchCloudUploadResult uploadBatch(application::model::SongResourceBatchCloudUploadRequest request) override;

private:
    BaiduPcsGoConfig config_;      // 上传配置
    CommandRunner command_runner_; // 命令执行器，测试可替换
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
