#pragma once

#include "crawler/application/model/SongSourceMetadata.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

// 列表 duration 和详情 length 统一为秒，无法解析时返回 0。
std::uint32_t parseSongDuration(const nlohmann::json &song);

// 兼容列表 singerList 与详情 artists，不把合唱展示文本当成独立歌手。
std::vector<application::model::SongSourceArtistRef> parseSourceArtists(const nlohmann::json &item);

// 兼容 Android/iOS 字段形状；只提取实际提供的音质元数据。
std::vector<application::model::SongSourceAudioFormat> parseSourceAudioFormats(const nlohmann::json &song);

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
