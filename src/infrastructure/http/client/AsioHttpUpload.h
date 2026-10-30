#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ssl/context.hpp>

#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 在既有客户端执行器上发送一个独占请求；不会复制正文或创建额外线程。
HttpUploadResult performStreamUpload(boost::asio::any_io_executor executor, boost::asio::ssl::context &tls, AsioHttpClient::Options options, std::string url, HttpUploadBody body, std::vector<std::string> headers, HttpRequestOptions overrides);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
