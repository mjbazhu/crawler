#pragma once

#include "crawler/application/ports/provider/AsyncSongResourceContentFetcher.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::http::client {

// 基于 AsioHttpClient 的异步歌曲资源流式下载器。
class AsyncHttpSongResourceContentFetcher : public application::ports::AsyncSongResourceContentFetcher {
public:
    explicit AsyncHttpSongResourceContentFetcher(std::shared_ptr<AsioHttpClient> client);

    // 异步流式下载歌曲资源内容，分片直接交付给异步 sink。
    application::async::Task<application::model::SongResourceContentStreamResult> streamSongResourceContent(
        application::model::SongResourceContentFetchRequest request,
        application::ports::AsyncSongResourceContentStreamCallbacks callbacks
    ) override;

private:
    std::shared_ptr<AsioHttpClient> client_; // Asio HTTP client
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client
