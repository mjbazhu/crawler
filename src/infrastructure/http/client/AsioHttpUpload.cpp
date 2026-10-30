#include "AsioHttpUpload.h"

#include "AsioHttpUploadStream.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

HttpUploadResult performStreamUpload(asio::any_io_executor executor, ssl::context &tls, AsioHttpClient::Options options, std::string url, HttpUploadBody body, std::vector<std::string> headers, HttpRequestOptions overrides)
{
    const auto parsed = parseUrl(url);
    if (!parsed || parsed->host.find('@') != std::string::npos || url.find_first_of("\r\n") != std::string::npos || !body.read || body.size_bytes == 0 || body.size_bytes > (std::uint64_t {1} << 40)) return {{}, 0, false, "invalid_upload_request"};
    for (const auto &header : headers) {
        if (header.find_first_of("\r\n\0", 0, 3) != std::string::npos) return {{}, 0, false, "invalid_upload_request"};
        auto name = header.substr(0, header.find(':'));
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "transfer-encoding" || name == "expect") return {{}, 0, false, "invalid_upload_request"};
    }
    if (overrides.request_timeout.count() > 0) options.request_timeout = overrides.request_timeout;
    if (overrides.max_response_bytes) options.max_response_bytes = overrides.max_response_bytes;
    const auto head = buildRequestHead("POST", *parsed, body.size_bytes, headers, proxyEnabled(options) ? &*options.proxy : nullptr);
    try {
        auto strand = asio::make_strand(executor);
        if (parsed->tls) return asio::co_spawn(strand, coUploadStream<TlsStream>(&tls, *parsed, head, options, std::move(body)), asio::use_future).get();
        return asio::co_spawn(strand, coUploadStream<TcpSocket>(nullptr, *parsed, head, options, std::move(body)), asio::use_future).get();
    } catch (...) {
        return {{}, 0, false, "upload_failed"};
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
