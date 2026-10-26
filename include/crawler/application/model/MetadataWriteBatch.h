#pragma once

#include <cstdint>
#include <string>

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceMetadata.h"
#include "crawler/application/model/SongSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 已提交事务的文件补偿信息；由 application 清理已被新封面替换的旧文件。
struct MetadataWriteResult {
    std::vector<std::string> replaced_cover_paths; // 仅包含本事务替换的本地封面路径
};

// 索引归属与歌手实体分开收集，避免同一歌手跨多个 tab 时被去重丢失。
struct SingerIndexMembership {
    std::string source;            // 平台标识
    std::string source_singer_id;  // 歌手来源身份
    std::string tab;               // 分类入口
    std::uint32_t index_order = 0; // 分类内顺序
    std::string source_tag;        // 分类标签
    std::string source_metric;     // 分类热度原始值
};

// 一个采集响应的持久化单位，实体和关系必须在同一事务中完成。
struct MetadataWriteBatch {
    std::vector<SingerSourceMetadata> singers;      // 已归一化的歌手来源
    std::vector<AlbumSourceMetadata> albums;        // 已归一化的专辑来源
    std::vector<SongSourceMetadata> songs;          // 歌曲来源及待建立关系
    std::vector<SingerIndexMembership> memberships; // 独立的分类归属
};

} // namespace orchestrator::modules::crawler::application::model
