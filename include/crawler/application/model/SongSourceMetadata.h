#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 歌曲平台音质参数，保存后续获取音频资源所需的稳定参数。
struct SongSourceAudioFormat {
    std::string format_type;   // 音质档位，例如 PQ、HQ、SQ、ZQ24
    std::string resource_type; // 平台资源类型，例如 Migu resourceType
    std::string iformat;       // iOS 资源格式编码
    std::string aformat;       // Android 资源格式编码
    std::uint64_t isize = 0;   // iOS 资源大小，单位字节
    std::uint64_t asize = 0;   // Android 资源大小，单位字节
};

// 歌曲与平台歌手的关系参数，支持合唱和多歌手顺序。
struct SongSourceArtistRef {
    std::string source_artist_id;          // 平台歌手 ID，仅用于入库前关联 singer_source_refs
    std::string source_artist_name;        // 平台歌手名称，仅作为解析阶段显示和调试信息
    std::string source_artist_name_pinyin; // 平台歌手拼音，仅用于回填 singer_source_refs
    std::string role = "artist";           // 歌手角色，当前 Migu singerList 统一为 artist
    std::uint32_t artist_order = 0;        // 歌手在平台返回列表中的顺序，从 1 开始
    bool is_primary = false;               // 是否主歌手
    bool is_seed = false;                  // 是否本次采集入口歌手
};

// 歌曲平台来源元数据，承载公共歌曲信息、平台 ID、平台时长、资源入口和音质参数。
struct SongSourceMetadata {
    std::string source;               // 平台标识，例如 migu
    std::string source_song_id;       // 平台歌曲 ID，例如 Migu songId
    std::string source_content_id;    // 平台内容或资源 ID，例如 Migu contentId/resId
    std::string source_resource_type; // 平台资源类型，例如 Migu resourceType

    std::string title;                         // 公共歌曲标题
    std::string normalized_title;              // 标准化标题，当前可与 title 一致
    std::uint32_t source_duration_seconds = 0; // 平台返回的歌曲时长，单位秒

    std::string source_title_pinyin;        // 平台歌曲拼音或排序名
    std::string source_album_id;            // 平台专辑 ID
    std::string source_album_material_id;   // 歌曲自带 albumId，用于数字专辑的明确身份对应，仅参与入库编排
    std::string source_album_name;          // 平台专辑名称
    std::string source_album_pinyin;        // 平台专辑拼音或排序名
    std::string source_primary_artist_id;   // 解析兜底主歌手 ID，不再写入 song_source_refs
    std::string source_primary_artist_name; // 解析兜底主歌手名称，不再写入 song_source_refs

    std::string cover_url; // 封面远程地址或平台路径
    std::string lyric_url; // 歌词远程地址
    std::string mrc_url;   // 逐字歌词远程地址
    std::string trc_url;   // 翻译歌词或扩展歌词远程地址

    std::string copyright_id; // 平台版权 ID；资源链路稳定前暂保留

    std::string album_disc_name;        // 专辑内 Disc 原始文本，例如 Disc 1
    std::uint32_t album_disc_no = 0;    // 专辑内 Disc 序号，未知时为 0
    std::uint32_t album_track_no = 0;   // 专辑内曲序，未知时为 0
    std::uint32_t album_song_order = 0; // 平台返回的专辑歌曲顺序，从 1 开始

    std::vector<SongSourceArtistRef> artist_refs;     // 歌曲与平台歌手关系
    std::vector<SongSourceAudioFormat> audio_formats; // 后续音频资源获取参数
};

} // namespace orchestrator::modules::crawler::application::model
