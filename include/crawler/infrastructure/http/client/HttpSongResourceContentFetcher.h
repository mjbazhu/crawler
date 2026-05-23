#pragma once

#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::http::client {

// 基于项目 HttpClient 的歌曲资源内容下载器。
class HttpSongResourceContentFetcher : public application::ports::SongResourceContentFetcher {
public:
    // 保存线程安全 HTTP client；client 为空时下载会失败。
    explicit HttpSongResourceContentFetcher(std::shared_ptr<HttpClient> client);

    // 下载歌曲资源内容，200 且响应体非空才视为成功。
    application::model::SongResourceContentFetchResult fetchSongResourceContent(application::model::SongResourceContentFetchRequest request) override;

    // 流式下载歌曲资源内容，200 且至少写入一个 body 分片才视为成功。
    application::model::SongResourceContentStreamResult streamSongResourceContent(application::model::SongResourceContentFetchRequest request, application::model::SongResourceContentStreamCallbacks callbacks) override;

private:
    std::shared_ptr<HttpClient> client_; // 出站 HTTP client
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client
