#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源远端复制 port，用于 rclone 等服务端拉取型存储 adapter。
class AsyncSongResourceRemoteCopier {
public:
    virtual ~AsyncSongResourceRemoteCopier() = default;

    // 将资源 URL 复制到目标存储；成功必须表示远端对象已提交且可验证。
    virtual application::async::Task<application::model::SongResourceRemoteCopyResult> copyUrl(application::model::SongResourceRemoteCopyRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
