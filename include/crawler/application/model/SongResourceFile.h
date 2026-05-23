#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 歌曲资源内容获取请求，面向已解析出的临时资源 URL。
struct SongResourceContentFetchRequest {
    std::string url;                          // 资源远程 URL
    std::vector<std::string> request_headers; // 下载资源时附加的 HTTP header
    int timeout_ms = 0;                       // 单次下载超时；0 表示使用客户端默认值
};

// 歌曲资源内容获取结果，保留 HTTP 状态和失败分类供 worker 回写。
struct SongResourceContentFetchResult {
    bool ok = false;            // 是否成功拿到可保存的响应体
    int status_code = 0;        // HTTP 状态码；网络失败时为 0
    std::string content_type;   // 响应 Content-Type
    std::vector<char> body;     // 响应体字节
    std::string error_category; // 失败分类，例如 network、resource
    std::string result_code;    // 结果编码，例如 ok、network_failed、empty_body
    std::string message;        // 脱敏后的诊断说明
};

// 歌曲资源流式响应头信息，供 workflow 在首个 body 分片前规划路径和打开写入会话。
struct SongResourceContentStreamInfo {
    int status_code = 0;      // HTTP 状态码
    std::string content_type; // 响应 Content-Type
};

// 歌曲资源流式下载回调；返回 false 表示下游写入失败并中止下载。
struct SongResourceContentStreamCallbacks {
    std::function<bool(const SongResourceContentStreamInfo &info)> on_response; // 响应头已可用
    std::function<bool(std::string_view chunk)> on_chunk;                       // 响应体分片
};

// 歌曲资源流式下载结果，不携带响应体，避免大文件驻留内存。
struct SongResourceContentStreamResult {
    bool ok = false;                  // 是否完整流式读取成功
    int status_code = 0;              // HTTP 状态码；网络失败时为 0
    std::string content_type;         // 响应 Content-Type
    std::uint64_t bytes_received = 0; // 已交付给下游的响应体字节数
    std::string error_category;       // 失败分类，例如 network、resource
    std::string result_code;          // 结果编码，例如 ok、network_failed、empty_body
    std::string message;              // 脱敏后的诊断说明
};

// 歌曲资源路径规划请求，使用稳定平台 ID 生成本地相对路径。
struct SongResourcePathRequest {
    std::uint64_t resource_id = 0;        // song_resources.id，兜底路径标识
    std::uint64_t song_source_ref_id = 0; // song_source_refs.id，兜底路径标识
    std::string source = "migu";          // 音乐平台标识
    std::string source_content_id;        // 平台内容 ID，优先作为歌曲目录名
    std::string source_song_id;           // 平台歌曲 ID，contentId 缺失时兜底
    std::string resource_kind;            // 资源类型：audio、cover、lyric、mrc、trc
    std::string format_type;              // 音频音质档位，非音频为空
    std::string remote_url;               // 资源远程 URL，用于推断扩展名
    std::string content_type;             // 响应 Content-Type，用于扩展名兜底
};

// 歌曲资源路径规划结果。
struct SongResourcePathResult {
    bool ok = false;           // 是否生成可用相对路径
    std::string relative_path; // 存储根目录下的相对路径
    std::string extension;     // 最终使用的文件扩展名，不含点
    std::string error;         // 失败原因
};

// 歌曲资源字节保存请求，relative_path_hint 由路径规划器生成。
struct SongResourceBlobSaveRequest {
    std::string relative_path_hint; // 期望保存的相对路径
    std::vector<char> data;         // 待保存资源字节
    std::string content_type;       // 资源 Content-Type
};

// 歌曲资源流式保存请求，写入实现负责临时对象和最终提交。
struct SongResourceBlobStreamSaveRequest {
    std::string relative_path_hint; // 期望保存的相对路径
    std::string content_type;       // 资源 Content-Type
};

// 歌曲资源字节保存结果，用于成功回写 song_resources。
struct SongResourceBlobSaveResult {
    bool ok = false;              // 是否保存成功
    std::string local_path;       // 存储系统返回的本地或对象相对路径
    std::uint64_t size_bytes = 0; // 保存字节数
    std::string checksum;         // SHA-256 校验值
    std::string error;            // 失败原因
};

} // namespace orchestrator::modules::crawler::application::model
