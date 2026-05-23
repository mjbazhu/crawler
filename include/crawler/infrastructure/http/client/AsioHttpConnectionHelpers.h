#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"
#include "crawler/infrastructure/http/client/AsioHttpSessionTypes.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <boost/asio/ssl/host_name_verification.hpp>
#include <openssl/ssl.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// HTTPS 代理 CONNECT 响应校验结果。
struct ProxyConnectValidation {
    bool complete = false;  // 响应头是否已完整读取
    bool success = false;   // CONNECT 是否成功
    std::string error;      // 失败原因
};

// 校验 HTTPS 代理 CONNECT 响应头，保持 callback 和 coroutine 两条路径行为一致。
inline ProxyConnectValidation validateProxyConnectResponse(const std::string &proxy_response)
{
    const std::size_t header_end = findHeaderEnd(proxy_response);
    if (header_end == std::string::npos) {
        if (proxy_response.size() > kMaxHeaderBytes) {
            return {true, false, "[AsioHttpClient] Proxy CONNECT response header is too large"};
        }
        return {false, false, {}};
    }

    const int proxy_status = parseStatusCode(std::string_view(proxy_response).substr(0, header_end));
    if (proxy_status != 200) {
        return {true, false, "[AsioHttpClient] Proxy CONNECT failed with status: " + std::to_string(proxy_status)};
    }
    if (proxy_response.size() > header_end) {
        return {true, false, "[AsioHttpClient] Proxy CONNECT returned unexpected body bytes"};
    }
    return {true, true, {}};
}

// 配置 TLS SNI 和证书校验策略；握手本身仍由调用方按 async/coroutine 方式执行。
template <typename Stream>
bool configureTlsStream(Stream &stream, const ParsedUrl &url, const AsioHttpClient::Options &options, std::string &error)
{
    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str())) {
        error = "[AsioHttpClient] Failed to set TLS SNI";
        return false;
    }

    stream.set_verify_mode(options.verify_tls ? ssl::verify_peer : ssl::verify_none);
    if (options.verify_tls) {
        stream.set_verify_callback(ssl::host_name_verification(url.host));
    }
    return true;
}

// 追加响应分片并检查响应大小上限。
inline bool appendResponseChunk(
    std::string &raw_response,
    const char *data,
    std::size_t bytes_read,
    std::size_t max_response_bytes,
    std::string &error
)
{
    if (bytes_read == 0) {
        return true;
    }

    raw_response.append(data, bytes_read);
    if (raw_response.size() > max_response_bytes + kMaxHeaderBytes) {
        error = "[AsioHttpClient] Response is too large";
        return false;
    }
    return true;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
