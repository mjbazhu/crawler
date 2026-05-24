#pragma once

#include "crawler/domain/music/model/Singer.h"

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 歌手平台来源元数据，承载公共歌手字段和 singer_source_refs 后续入库参数。
struct SingerSourceMetadata {
    std::string source;                              // 平台标识，例如 migu
    std::string name;                                // 公共歌手名称
    std::string normalized_name;                     // 标准化歌手名，当前可与 name 一致
    domain::Gender gender = domain::Gender::Unknown; // 由 tab 推导的性别，用于 singers.gender

    std::string source_item_key;   // 平台来源唯一业务键，例如 singer:112
    std::string source_singer_id;  // 平台歌手 ID，例如 Migu resId=112
    std::string source_view_id;    // 平台 viewId，例如 4002-112
    std::string source_res_type;   // 平台资源类型，例如 2002
    std::string source_action;     // 平台跳转 action，例如 mgmusic://singer-info?id=112
    std::string source_track;      // 平台埋点或来源标识，例如 gsgc_flgs
    std::string source_display_id; // 平台展示 ID，例如 Migu txt2=112
    std::string source_name_pinyin; // 平台歌手拼音或排序名，平台未返回时为空
    std::string source_tag;        // 平台标签或歌手类型，例如 热
    std::string source_metric;     // 平台热度、播放量或数量原始值
    std::string source_country;    // 由 tab 推导的平台地区，例如 华语、欧美、日韩
    std::string avatar_url;        // 平台头像地址
    std::string source_index_tab;  // 平台歌手分类 tab，例如 huayu-nan
    std::uint32_t source_index_order = 0; // 歌手在当前分类 tab 中的顺序，从 1 开始
};

} // namespace orchestrator::modules::crawler::application::model
