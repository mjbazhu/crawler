#pragma once

#include "crawler/application/ports/provider/AsyncSongResourceProvider.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <memory>
#include <optional>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu 异步歌曲资源 provider，使用 Asio HTTP 协程获取 listen 原始响应。
class MiguAsyncSongResourceProvider : public application::ports::AsyncSongResourceProvider {
public:
    MiguAsyncSongResourceProvider(
        std::shared_ptr<orchestrator::modules::crawler::infrastructure::http::client::AsioHttpClient> client,
        std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
    );

    // 异步获取歌曲资源接口原始响应。
    application::async::Task<std::optional<Response>> fetchSongResource(application::model::SongResourceFetchRequest request) override;

private:
    std::shared_ptr<orchestrator::modules::crawler::infrastructure::http::client::AsioHttpClient> client_; // Asio HTTP client
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store_;  // 运行期 pacmtoken
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
