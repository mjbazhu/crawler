#pragma once

#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu pacmtoken 刷新客户端，只封装 checkPacMtoken 平台调用。
class MiguPacmtokenClient : public application::ports::MiguPacmtokenRefresher {
public:
    // 构造函数：复用统一出站 HTTP client。
    explicit MiguPacmtokenClient(std::shared_ptr<http::client::HttpClient> client);

    // 调用 checkPacMtoken，并从响应头或 Set-Cookie 提取新 pacmtoken。
    application::model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string pacmtoken) override;

private:
    std::shared_ptr<http::client::HttpClient> client_; // 出站 HTTP client
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
