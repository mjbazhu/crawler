#pragma once

#include "crawler/application/model/ProviderResponse.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲搜索 provider port，只负责获取歌曲搜索原始响应。
class SongSearchProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~SongSearchProvider() = default;

    // 获取歌曲搜索单页原始响应，调用方负责解析和分页策略。
    virtual std::optional<Response> fetchSongSearchPage(std::string keyword, int page, int rows) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
