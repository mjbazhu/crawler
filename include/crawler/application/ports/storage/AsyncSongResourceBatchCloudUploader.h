#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源批量云端上传 port，隔离 BaiduPCS-Go 等阻塞上传实现。
class AsyncSongResourceBatchCloudUploader {
public:
    virtual ~AsyncSongResourceBatchCloudUploader() = default;

    // 异步上传本地批次；成功必须返回每个本地路径对应的稳定云端路径。
    virtual application::async::Task<application::model::SongResourceBatchCloudUploadResult> uploadBatch(application::model::SongResourceBatchCloudUploadRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
