#pragma once

#include "crawler/application/model/ProviderResponse.h"

#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 歌手目录 provider port，只负责获取歌手歌曲、专辑和分类列表原始响应。
class SingerCatalogProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~SingerCatalogProvider() = default;

    // 获取歌手歌曲单页原始响应，调用方负责解析分页结束条件。
    virtual std::optional<Response> fetchSingerSongsPage(std::uint64_t singer_id, int page) = 0;

    // 获取歌手专辑单页原始响应，调用方负责解析分页结束条件。
    virtual std::optional<Response> fetchSingerAlbumsPage(std::uint64_t singer_id, int page) = 0;

    // 获取歌手分类列表原始响应，tab 由具体平台 provider 定义和校验。
    virtual std::optional<Response> fetchSingerIndexPage(std::string tab) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
