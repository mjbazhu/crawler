#pragma once

#include "crawler/application/ports/storage/SongResourceCloudUploader.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoConfig.h"

#include <functional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {

// BaiduPCS-Go 本地文件上传 adapter；线程安全：无共享可变状态，upload 可并发调用。
class BaiduPcsGoUploader : public application::ports::SongResourceCloudUploader {
public:
    using CommandRunner = std::function<int(const std::string &command)>;

    explicit BaiduPcsGoUploader(BaiduPcsGoConfig config, CommandRunner command_runner = {});

    // 将本地缓存文件上传到百度网盘 cloud_root 下同名相对路径。
    application::model::SongResourceCloudUploadResult upload(application::model::SongResourceCloudUploadRequest request) override;

private:
    BaiduPcsGoConfig config_;      // 上传配置
    CommandRunner command_runner_; // 命令执行器，测试可替换
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
