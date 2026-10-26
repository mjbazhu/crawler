#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源云端上传 port，隔离 BaiduPCS-Go、rclone 等阻塞上传实现。
class AsyncSongResourceCloudUploader {
public:
    virtual ~AsyncSongResourceCloudUploader() = default;

    // 异步上传本地文件；成功必须返回稳定云端路径。
    virtual application::async::Task<application::model::SongResourceCloudUploadResult> upload(application::model::SongResourceCloudUploadRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
