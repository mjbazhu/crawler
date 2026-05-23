#pragma once

#include "crawler/application/model/ProviderResponse.h"
#include "crawler/application/model/SongResourceFetch.h"

#include <optional>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源 provider port，只负责用已入库的歌曲来源参数换取平台资源接口原始响应。
class SongResourceProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~SongResourceProvider() = default;

    // 获取歌曲资源接口原始响应，调用方负责解析 URL、下载和回写资源任务。
    virtual std::optional<Response> fetchSongResource(application::model::SongResourceFetchRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
