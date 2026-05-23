#pragma once

#include "crawler/domain/music/model/ParseResult.h"
#include "crawler/domain/music/model/Song.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy::detail {

// 选择第一个非空字符串，减少平台字段回退分支。
std::string firstNonEmpty(std::initializer_list<std::string> values);

// 将有效歌曲加入解析结果，并统一生成建议文件名。
void addSongIfValid(domain::Song song, std::vector<domain::ParseResult> &results);

// 判断 Migu 卡片是否包含可入库歌曲信息。
bool hasSongItem(const nlohmann::json &item);

// 从歌手歌曲接口的卡片中提取当前 Song 模型支持的字段。
domain::Song parseSongFromSingerSongItem(const nlohmann::json &item);

// 从数组首项提取名称和无符号 ID，用于 singer/album 字段。
void extractUnsignedFromFirstItem(
    const nlohmann::json &item,
    const std::string &array_key,
    const std::string &name_key,
    std::string &name_value,
    const std::string &id_key,
    std::uint64_t &id_value
);

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy::detail
