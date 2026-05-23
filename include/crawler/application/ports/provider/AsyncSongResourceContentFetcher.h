#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceFile.h"

#include <functional>
#include <span>

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源流式下载回调；下游写入可 co_await，避免阻塞 I/O 协程。
struct AsyncSongResourceContentStreamCallbacks {
    std::function<application::async::Task<bool>(const application::model::SongResourceContentStreamInfo &info)> on_response; // 响应头已可用
    std::function<application::async::Task<bool>(std::span<const char> chunk)> on_chunk;                                      // 响应体分片
};

// 异步歌曲资源内容获取 port，只负责把资源 URL 流式交付给下游。
class AsyncSongResourceContentFetcher {
public:
    virtual ~AsyncSongResourceContentFetcher() = default;

    // 异步流式下载资源内容；结果不携带完整 body，避免大文件驻留内存。
    virtual application::async::Task<application::model::SongResourceContentStreamResult> streamSongResourceContent(
        application::model::SongResourceContentFetchRequest request,
        AsyncSongResourceContentStreamCallbacks callbacks
    ) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
