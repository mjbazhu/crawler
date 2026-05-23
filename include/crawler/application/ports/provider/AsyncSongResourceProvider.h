#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/ProviderResponse.h"
#include "crawler/application/model/SongResourceFetch.h"

#include <optional>

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源 provider port，只负责用资源参数换取平台 listen 原始响应。
class AsyncSongResourceProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~AsyncSongResourceProvider() = default;

    // 异步获取歌曲资源接口原始响应；调用方负责解析、下载和回写。
    virtual application::async::Task<std::optional<Response>> fetchSongResource(application::model::SongResourceFetchRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
