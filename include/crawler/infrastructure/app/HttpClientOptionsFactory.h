#pragma once

#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"

namespace orchestrator::modules::crawler::infrastructure::app {

// 根据启动配置生成出站 HTTP client 选项。
http::client::AsioHttpClient::Options makeHttpClientOptions(const ServerApplication::Config &config);

} // namespace orchestrator::modules::crawler::infrastructure::app
