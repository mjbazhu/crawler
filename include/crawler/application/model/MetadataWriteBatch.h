#pragma once

#include "crawler/application/model/SingerSourceProfile.h"

#include <cstdint>
#include <string>

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceMetadata.h"
#include "crawler/application/model/SongSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 事务产生的歌曲来源主键，供 application 消费当前响应中的临时关联资源。
struct PersistedSongSource {
    std::uint64_t source_ref_id = 0; // 已提交来源主键
    std::string source;              // 平台身份
    std::string song_id;             // 平台歌曲身份
    std::string content_id;          // 资源内容身份
};

// 已提交事务的文件补偿信息；由 application 清理已替换或本次未采用的封面。
struct MetadataWriteResult {
    std::vector<std::string> replaced_cover_paths;       // 提交后可清理的旧封面或本事务未采用的新封面
    std::vector<PersistedSongSource> saved_song_sources; // 本事务已保存的歌曲来源
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

// 一次采集及必要分页补齐的持久化单位，实体和关系必须在同一事务中完成。
struct MetadataWriteBatch {
    std::vector<SingerSourceMetadata> singers;      // 已归一化的歌手来源
    std::vector<AlbumSourceMetadata> albums;        // 已归一化的专辑来源
    std::vector<SongSourceMetadata> songs;          // 歌曲来源及待建立关系
    std::vector<SingerSourceProfile> profiles;      // 平台声明总数与实体在同一事务保存
    std::vector<SingerIndexMembership> memberships; // 独立的分类归属
};

} // namespace orchestrator::modules::crawler::application::model
