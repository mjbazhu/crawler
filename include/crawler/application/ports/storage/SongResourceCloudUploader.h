#pragma once

#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源云端上传 port，只负责把已落地的本地资源上传到目标网盘。
class SongResourceCloudUploader {
public:
    virtual ~SongResourceCloudUploader() = default;

    // 上传本地文件；成功必须返回稳定云端路径。
    virtual application::model::SongResourceCloudUploadResult upload(application::model::SongResourceCloudUploadRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
