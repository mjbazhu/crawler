#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"

#include <chrono>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {

http::client::AsioHttpClient::Options makeHttpClientOptions(const ServerApplication::Config &config)
{
    http::client::AsioHttpClient::Options options;
    options.io_threads = config.outbound_http_io_threads;
    options.request_timeout = std::chrono::milliseconds(config.outbound_http_timeout_ms);
    options.max_response_bytes = config.outbound_http_max_response_bytes;
    options.verify_tls = config.outbound_http_verify_tls;

    if (!config.outbound_proxy_host.empty() && !config.outbound_proxy_port.empty()) {
        http::client::AsioHttpClient::ProxyOptions proxy;
        proxy.host = config.outbound_proxy_host;
        proxy.port = config.outbound_proxy_port;
        proxy.username = config.outbound_proxy_username;
        proxy.password = config.outbound_proxy_password;
        options.proxy = std::move(proxy);
    }

    return options;
}

} // namespace orchestrator::modules::crawler::infrastructure::app
