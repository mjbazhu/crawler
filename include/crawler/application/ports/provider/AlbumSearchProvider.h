#pragma once

#include "crawler/application/model/ProviderResponse.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 专辑搜索 provider port，只负责获取专辑搜索原始响应。
class AlbumSearchProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~AlbumSearchProvider() = default;

    // 获取专辑搜索单页原始响应，调用方负责解析和分页策略。
    virtual std::optional<Response> fetchAlbumSearchPage(std::string keyword, int page, int rows) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
