#pragma once

#include "crawler/application/model/ProviderResponse.h"
#include "crawler/domain/music/model/Album.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 专辑详情 provider port，只负责获取专辑相关原始响应。
class AlbumDetailProvider {
public:
    using Response = application::model::ProviderResponse;

    virtual ~AlbumDetailProvider() = default;

    // 获取专辑歌曲原始响应，调用方负责解析。
    virtual std::optional<Response> fetchAlbumSongs(const domain::Album &album) = 0;

    // 获取平台专辑详情原始响应，album_id 为平台专辑或数字专辑 ID。
    virtual std::optional<Response> fetchAlbumDetail(std::string album_id, std::string source_res_type) = 0;

    // 获取有界专辑曲目页；旧 adapter 未实现时明确返回不可用，不能重复取详情冒充下一页。
    virtual std::optional<Response> fetchAlbumSongPage(std::string, int, int)
    {
        return std::nullopt;
    }
};

} // namespace orchestrator::modules::crawler::application::ports
