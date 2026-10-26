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
    std::string checksum;         // 存储校验值，文件存储为 SHA-256，远端存储可带算法前缀
    std::string error;            // 失败原因
};

// 歌曲资源云端上传请求，输入必须是本地 blob store 已提交的相对路径。
struct SongResourceCloudUploadRequest {
    std::string local_path;       // 本地存储根目录下的相对路径
    std::uint64_t size_bytes = 0; // 本地文件大小，供上传后回写和校验
    std::string checksum;         // 本地文件校验值，上传成功后沿用到资源记录
    int timeout_ms = 0;           // 单次上传等待超时；0 表示使用 adapter 默认值
};

// 歌曲资源云端上传结果，成功后由 workflow 和本地保存结果一起写库。
struct SongResourceCloudUploadResult {
    bool ok = false;              // 是否完成云端上传
    std::string backend;          // 上传后端，例如 baidupcs、rclone
    std::string cloud_path;       // 云端保存路径
    std::uint64_t size_bytes = 0; // 云端对象大小；无法查询时沿用本地大小
    std::string checksum;         // 云端对象校验值或沿用本地校验值
    std::string error_category;   // 失败分类，例如 persistence、timeout、unknown
    std::string result_code;      // 结果编码，例如 ok、cloud_upload_failed
    std::string message;          // 脱敏后的诊断说明
};

// 批量云端上传对象，输入必须已经保存到本地存储根目录下。
struct SongResourceBatchCloudUploadObject {
    std::uint64_t resource_id = 0; // song_resources.id
    std::string local_path;        // 本地存储根目录下的相对路径
    std::uint64_t size_bytes = 0;  // 本地文件大小
    std::string checksum;          // 本地文件校验值
};

// 批量云端上传请求，用于一次性上传一个资源处理批次。
struct SongResourceBatchCloudUploadRequest {
    std::string batch_id;                                      // 当前处理批次 ID
    std::vector<SongResourceBatchCloudUploadObject> objects;   // 待上传对象
    int timeout_ms = 0;                                        // 单次批量上传等待超时；0 表示 adapter 默认值
};

// 批量云端上传对象结果，成功后给 workflow 回写每条资源的云端路径。
struct SongResourceBatchCloudUploadObjectResult {
    std::uint64_t resource_id = 0; // song_resources.id
    std::string local_path;        // 本地相对路径
    std::string cloud_path;        // 云端保存路径
};

// 批量云端上传结果，adapter 负责保证返回路径稳定可回写。
struct SongResourceBatchCloudUploadResult {
    bool ok = false;                                                   // 是否完成整批上传
    std::string backend;                                               // 上传后端，例如 baidupcs
    std::vector<SongResourceBatchCloudUploadObjectResult> objects;     // 每条对象的云端路径
    std::string error_category;                                        // 失败分类
    std::string result_code;                                           // 结果编码
    std::string message;                                               // 脱敏诊断说明
};

// 歌曲资源远端 URL 复制请求，由远端存储服务直接拉取资源 URL。
struct SongResourceRemoteCopyRequest {
    std::string source_url;         // 待复制的资源临时 URL
    std::string relative_path_hint; // 期望写入的对象相对路径
    int timeout_ms = 0;             // 单次远端复制等待超时；0 表示使用 adapter 默认值
};

// 歌曲资源远端 URL 复制结果，成功后可直接用于 song_resources 回写。
struct SongResourceRemoteCopyResult {
    bool ok = false;              // 是否完成远端复制并通过验证
    std::string local_path;       // 存储系统返回的对象相对路径
    std::uint64_t size_bytes = 0; // 远端对象大小
    std::string checksum;         // 存储校验值或可诊断的远端对象指纹
    std::string error_category;   // 失败分类，例如 network、timeout、persistence
    std::string result_code;      // 结果编码，例如 ok、rclone_job_failed
    std::string message;          // 脱敏后的诊断说明
};

} // namespace orchestrator::modules::crawler::application::model
