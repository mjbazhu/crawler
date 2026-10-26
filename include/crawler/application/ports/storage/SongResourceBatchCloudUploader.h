#pragma once

#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源批量云端上传 port，只负责把已落地的一批本地资源上传到目标网盘。
class SongResourceBatchCloudUploader {
public:
    virtual ~SongResourceBatchCloudUploader() = default;

    // 上传本地批次；成功必须返回每个本地路径对应的稳定云端路径。
    virtual application::model::SongResourceBatchCloudUploadResult uploadBatch(application::model::SongResourceBatchCloudUploadRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
