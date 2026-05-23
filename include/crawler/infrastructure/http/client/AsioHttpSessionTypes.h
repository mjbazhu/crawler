#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <cstddef>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;

using TcpSocket = asio::ip::tcp::socket;
using TlsStream = ssl::stream<TcpSocket>;
using ErrorCode = boost::system::error_code;
using IoStrand = asio::strand<asio::io_context::executor_type>;

inline constexpr std::size_t kReadBufferSize = 8192; // 单次 socket 读取缓冲区大小

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
