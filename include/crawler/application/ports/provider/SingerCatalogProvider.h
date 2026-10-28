#pragma once

#include "crawler/application/model/ProviderResponse.h"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 歌手目录 provider port，只负责获取歌手歌曲、专辑和分类列表原始响应。
class SingerCatalogProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~SingerCatalogProvider() = default;

    // 歌曲每页容量提示；0 表示未知，只用于并发调度，不作为目录结束依据。
    virtual std::size_t singerSongPageSize() const
    {
        return 0;
    }

    // 专辑每页容量提示；0 表示未知，具体平台独立定义。
    virtual std::size_t singerAlbumPageSize() const
    {
        return 0;
    }

    // 获取歌手歌曲单页原始响应，调用方负责解析分页结束条件。
    virtual std::optional<Response> fetchSingerSongsPage(std::uint64_t singer_id, int page) = 0;

    // 获取歌手主要信息原始响应，调用方负责解析平台声明总数。
    virtual std::optional<Response> fetchSingerInfo(std::uint64_t singer_id) = 0;

    // 获取歌手专辑单页原始响应，调用方负责解析分页结束条件。
    virtual std::optional<Response> fetchSingerAlbumsPage(std::uint64_t singer_id, int page) = 0;

    // 获取歌手分类列表原始响应，tab 由具体平台 provider 定义和校验。
    virtual std::optional<Response> fetchSingerIndexPage(std::string tab) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
