#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceMetadata.h"
#include "crawler/application/model/SingerSourceProfile.h"
#include "crawler/application/model/SongSourceMetadata.h"
#include "crawler/domain/music/model/Singer.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

// 选择第一个非空字符串，用于 Migu 多字段回退。
std::string firstNonEmpty(std::initializer_list<std::string> values);

// 从 mgmusic action 的 id 参数中提取平台 ID。
std::string extractActionId(const std::string &action);

// 从 viewId 末尾提取平台 ID，例如 4002-112 -> 112。
std::string extractViewIdSuffix(const std::string &view_id);

// 将平台返回的布尔、0/1 数字或字符串统一转换为三态标记。
int parseTriStateFlag(const nlohmann::json &item, std::string_view key);

// 根据 Migu 列表 resType 和详情 resourceType 归一平台专辑类型。
int albumKindFromTypes(std::string_view source_res_type, std::string_view source_resource_type);

// 从图片数组中选择最适合持久化的图片地址。
std::string selectImageUrl(const nlohmann::json *items);

// 将 Migu 返回的资源路径补齐为可直接请求的 URL。
std::string normalizeMiguResourceUrl(std::string resource_url);

// 歌手头像排除已确认的平台默认图，保留空值以沿用已有真实地址。
std::string normalizeMiguSingerAvatarUrl(std::string resource_url);

// 判断列表项是否包含有效歌曲信息。
bool hasValidSongItem(const nlohmann::json &item);

// 判断歌手专辑列表卡片是否包含有效专辑来源信息。
bool hasValidAlbumItem(const nlohmann::json &item);

// 从 Migu 歌手分类 tab 中获取地区和性别。
std::optional<domain::CountryGender> singerClassFromTab(std::string_view tab);

// 从 Migu 歌曲对象提取可直接写入来源表的元数据。
std::optional<application::model::SongSourceMetadata> parseSongSourceFromSongObject(
    const nlohmann::json &song_item,
    const nlohmann::json *wrapper,
    const nlohmann::json *album_context,
    std::uint32_t song_order = 0
);

// 从对象、数组和歌手歌曲分页结构中收集歌曲来源元数据。
void collectSongSources(const nlohmann::json &node, std::vector<application::model::SongSourceMetadata> &songs);

// 从 Migu 歌手卡片提取后续写入所需字段。
std::optional<application::model::SingerSourceMetadata> parseSingerSourceFromItem(
    const nlohmann::json &item,
    const domain::CountryGender &singer_class,
    std::string_view tab,
    std::uint32_t index_order
);

// 从对象、数组和歌手列表结构中收集歌手来源元数据。
void collectSingerSources(
    const nlohmann::json &node,
    const domain::CountryGender &singer_class,
    std::string_view tab,
    std::uint32_t &next_index_order,
    std::vector<application::model::SingerSourceMetadata> &singers
);

// 从 Migu 歌手主要信息响应中解析平台声明总数。
application::model::SingerSourceProfile parseSingerSourceProfileRaw(const std::vector<char> &raw_data);

// 复用已解码响应；只有完整资料请求才把确实缺失的目录 tab 解释为零。
application::model::SingerSourceProfile parseSingerSourceProfileObject(const nlohmann::json &root, bool complete_profile);

// 从 Migu 歌手专辑列表卡片提取专辑来源的初始参数。
std::optional<application::model::AlbumSourceMetadata> parseAlbumSourceFromListItem(const nlohmann::json &item);

// 从 Migu 专辑详情对象提取完整专辑来源参数。
std::optional<application::model::AlbumSourceMetadata> parseAlbumSourceFromDetailObject(const nlohmann::json &item);

// 从对象、数组、歌手专辑列表和专辑详情结构中收集专辑来源元数据。
void collectAlbumSources(const nlohmann::json &node, std::vector<application::model::AlbumSourceMetadata> &albums);

// 统计 data.contents[n].contents 中有效歌曲数量。
std::size_t countSingerSongsInContentBlock(const nlohmann::json &content_block);

// 统计歌手专辑列表响应中的有效专辑数量。
std::size_t countSingerAlbums(const nlohmann::json &node);

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
