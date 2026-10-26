#pragma once

#include <string>

namespace orchestrator::modules::crawler::application::model {

// 歌曲资源获取请求，承载从 song_source_refs 和 song_source_audio_formats 取出的稳定参数。
struct SongResourceFetchRequest {
    std::string content_id;      // 平台内容 ID，例如 Migu contentId
    std::string net_type = "01"; // Migu 网络类型参数，默认 01
    std::string resource_type;   // 平台资源类型，例如 Migu resourceType=2
    std::string copyright_id;    // 平台版权 ID
    std::string tone_flag;       // 平台音质档位，例如 PQ、HQ、ZQ24
};

// 歌曲资源获取结果，只承载资源接口返回的下载入口和附属资源 URL。
struct SongResourceFetchResult {
    bool ok = false;               // 是否可继续下载音频 URL
    std::string code;              // 平台响应码
    std::string message;           // 平台响应说明
    std::string url;               // 音频临时下载 URL
    std::string version;           // 资源版本标识
    std::string audio_format_type; // 实际返回音质档位
    std::string cover_url;         // 封面 URL
    std::string lyric_url;         // 行级歌词 URL
    std::string mrc_url;           // 逐字歌词或扩展歌词 URL
    std::string trc_url;           // 翻译歌词或扩展歌词 URL
};

} // namespace orchestrator::modules::crawler::application::model
