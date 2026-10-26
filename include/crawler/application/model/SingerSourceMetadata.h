#pragma once

#include "crawler/domain/music/model/Singer.h"
#include "crawler/application/model/MetadataResourceFile.h"

#include <cstdint>
#include <string>
#include <optional>

namespace orchestrator::modules::crawler::application::model {

// 歌手平台来源元数据，承载公共歌手字段和 singer_source_refs 后续入库参数。
struct SingerSourceMetadata {
    std::string source;                              // 平台标识，例如 migu
    std::string name;                                // 公共歌手名称
    std::string normalized_name;                     // 标准化歌手名，当前可与 name 一致
    domain::Gender gender = domain::Gender::Unknown; // 由 tab 推导的性别，用于 singers.gender

    std::string source_singer_id;                       // 平台歌手 ID，例如 Migu resId=112
    std::string source_name_pinyin;                     // 平台歌手拼音或排序名，平台未返回时为空
    std::string source_tag;                             // 平台标签或歌手类型，例如 热
    std::string source_metric;                          // 分类列表热度原始值，仅写入 singer_index_memberships
    std::string source_country;                         // 由 tab 推导的平台地区，例如 华语、欧美、日韩
    std::string avatar_url;                             // 本次采集的头像地址，仅用于立即下载，不持久化
    std::optional<MetadataResourceFile> cover_resource; // 本次封面保存结果
    std::string source_index_tab;                       // 平台歌手分类 tab，例如 huayu-nan
    std::uint32_t source_index_order = 0;               // 歌手在当前分类 tab 中的顺序，从 1 开始
};

// 歌手来源拼音回填参数，来自歌曲列表 singerList.nameSpelling。
struct SingerSourceNamePinyinPatch {
    std::string source;             // 平台标识，例如 migu
    std::string source_singer_id;   // 平台歌手 ID
    std::string source_name_pinyin; // 平台歌手拼音或排序名
};

} // namespace orchestrator::modules::crawler::application::model
