#pragma once

#include "crawler/application/model/ProviderResponse.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 原始页面 provider port，只负责按平台规则获取或归一化 URL 响应。
class RawPageFetcher {
public:
    using Response = application::model::ProviderResponse;

    virtual ~RawPageFetcher() = default;

    // 通过 URL 获取原始响应，具体平台 provider 负责 URL 归一化。
    virtual std::optional<Response> fetchUrl(std::string url) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
