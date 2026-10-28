#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/MusicCatalog.h"

#include <string_view>

namespace orchestrator::modules::crawler::application::model {

// 普通公共归档不能自动展开；明确的数字专辑类型或物料身份仍按真实专辑采集。
inline bool isSharedReleaseArchiveIdentity(std::string_view source, std::string_view id, std::string_view title, int kind, std::string_view resource_type, std::string_view res_type, std::string_view material_id)
{
    return source == "migu" && id == "1" && (title.empty() || title == "单曲发行")
           && kind != 2 && resource_type != "5" && res_type != "1"
           && (material_id.empty() || material_id == id);
}

// 解析输入和执行入口使用同一个公共归档判定。
inline bool isSharedReleaseArchive(const AlbumSourceMetadata &album)
{
    return isSharedReleaseArchiveIdentity(album.source, album.source_album_id, album.title, album.source_album_kind, album.source_resource_type, album.source_res_type, album.source_material_id);
}

// 回补预览和操作结果使用与执行入口一致的身份边界。
inline bool isSharedReleaseArchive(const MiguAlbumCatalogRecord &album)
{
    return isSharedReleaseArchiveIdentity(album.source, album.source_album_id, album.title, album.source_album_kind, album.source_resource_type, album.source_res_type, album.source_material_id);
}

} // namespace orchestrator::modules::crawler::application::model
