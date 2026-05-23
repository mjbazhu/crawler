#pragma once

#include "crawler/application/model/SongResourceFile.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源内容获取 port，只负责把资源 URL 换成响应字节。
class SongResourceContentFetcher {
public:
    virtual ~SongResourceContentFetcher() = default;

    // 下载资源内容；失败时返回 ok=false 并提供可回写的失败分类。
    virtual application::model::SongResourceContentFetchResult fetchSongResourceContent(application::model::SongResourceContentFetchRequest request) = 0;

    // 流式下载资源内容；默认实现走完整下载兼容旧测试替身，生产实现应覆盖为真实流式读取。
    virtual application::model::SongResourceContentStreamResult streamSongResourceContent(application::model::SongResourceContentFetchRequest request, application::model::SongResourceContentStreamCallbacks callbacks);
};

inline application::model::SongResourceContentStreamResult SongResourceContentFetcher::streamSongResourceContent(application::model::SongResourceContentFetchRequest request, application::model::SongResourceContentStreamCallbacks callbacks)
{
    application::model::SongResourceContentStreamResult result;
    auto fetched = fetchSongResourceContent(std::move(request));
    result.status_code = fetched.status_code;
    result.content_type = fetched.content_type;
    if (!fetched.ok) {
        result.error_category = std::move(fetched.error_category);
        result.result_code = std::move(fetched.result_code);
        result.message = std::move(fetched.message);
        return result;
    }

    application::model::SongResourceContentStreamInfo info;
    info.status_code = fetched.status_code;
    info.content_type = fetched.content_type;
    if (callbacks.on_response && !callbacks.on_response(info)) {
        result.error_category = "persistence";
        result.result_code = "stream_consumer_failed";
        result.message = "resource stream response was rejected by downstream writer";
        return result;
    }

    if (!fetched.body.empty()) {
        if (callbacks.on_chunk && !callbacks.on_chunk(std::string_view(fetched.body.data(), fetched.body.size()))) {
            result.error_category = "persistence";
            result.result_code = "stream_consumer_failed";
            result.message = "resource stream chunk write failed";
            return result;
        }
    }

    result.ok = true;
    result.bytes_received = static_cast<std::uint64_t>(fetched.body.size());
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::application::ports
