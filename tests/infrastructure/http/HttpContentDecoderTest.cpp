#include "crawler/infrastructure/http/client/HttpContentDecoder.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace http = orchestrator::modules::crawler::infrastructure::http::client;
namespace detail = http::detail;
namespace asio = boost::asio;

namespace {

// 所有断言在发布构建中仍然执行。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 生成标准 gzip，包括 CRC 和长度尾部，测试不依赖外部服务器。
std::string gzip(const std::string &input)
{
    z_stream stream {};
    require(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK, "gzip fixture initialization failed");
    std::string output(deflateBound(&stream, static_cast<uLong>(input.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const auto code = deflate(&stream, Z_FINISH);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    require(code == Z_STREAM_END, "gzip fixture did not finish");
    return output;
}

// 按单字节 chunk 分割压缩数据，覆盖传输与压缩边界互不对齐的情况。
std::string response(const std::string &encoded, bool chunked)
{
    std::string out = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Encoding: gzip\r\nConnection: close\r\n";
    if (!chunked) return out + "Content-Length: " + std::to_string(encoded.size()) + "\r\n\r\n" + encoded;
    out += "Transfer-Encoding: chunked\r\n\r\n";
    for (char byte : encoded) out += std::string("1\r\n") + byte + "\r\n";
    return out + "0\r\n\r\n";
}

// 动态本地端口提供一次固定响应，只用于真实客户端协议验证。
class Server {
public:
    explicit Server(std::string response) : acceptor_(io_, {asio::ip::make_address("127.0.0.1"), 0})
    {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this, response = std::move(response)] {
            boost::system::error_code error;
            asio::ip::tcp::socket socket(io_);
            acceptor_.accept(socket, error);
            if (error) return;
            asio::streambuf request;
            asio::read_until(socket, request, "\r\n\r\n", error);
            asio::write(socket, asio::buffer(response), error);
            socket.shutdown(asio::ip::tcp::socket::shutdown_both, error);
        });
    }

    // 每次请求都必须结束，测试线程不逃逸。
    ~Server()
    {
        if (thread_.joinable()) thread_.join();
    }

    // URL 只指向本测试动态端口。
    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_) + "/gzip";
    }

private:
    asio::io_context io_;              // 独占服务端上下文
    asio::ip::tcp::acceptor acceptor_; // 单次响应监听器
    std::thread thread_;               // 服务线程
    unsigned short port_ = 0;          // 动态监听端口
};

// 单字节压缩分片与解压上限、完整性校验分别验证。
void verifyDecoder()
{
    const auto encoded = gzip(std::string(40000, 'x'));
    detail::HttpContentDecoder decoder;
    require(decoder.configure("gzip", 50000), "gzip rejected");
    std::string all, chunk;
    for (const auto &byte : encoded) {
        require(decoder.decode(std::string_view(&byte, 1), chunk), "fragmented gzip failed");
        all += chunk;
    }
    require(decoder.finish() && all == std::string(40000, 'x') && decoder.wireBytes() == encoded.size(), "decoded output or wire count differs");
    detail::HttpContentDecoder limited;
    require(limited.configure("gzip", 1000), "limit fixture failed");
    require(!limited.decode(encoded, chunk) && limited.errorCode() == "body_too_large", "decompression bypassed size limit");
    detail::HttpContentDecoder unknown;
    require(!unknown.configure("br", 1000) && unknown.errorCode() == "unsupported_content_encoding", "unsupported encoding was accepted");
    auto damaged = encoded;
    damaged.back() ^= 1;
    detail::HttpContentDecoder corrupt;
    require(corrupt.configure("gzip", 50000) && !corrupt.decode(damaged, chunk), "gzip checksum damage was accepted");
    detail::HttpContentDecoder truncated;
    require(truncated.configure("gzip", 50000) && truncated.decode(encoded.substr(0, encoded.size() - 4), chunk) && !truncated.finish(), "truncated gzip was accepted");
}

// 三种生产客户端路径都必须解码 Content-Length 和 chunked 内容，并拒绝截断内容。
void verifyClients()
{
    const auto original = std::string("[00:01.00]歌词解码\n") + std::string(1000, 'a');
    const auto encoded = gzip(original);
    for (bool chunked : {false, true}) {
        const auto parsed = detail::parseHttpResponse(response(encoded, chunked), 10000);
        require(parsed && std::string(parsed->body.begin(), parsed->body.end()) == original, "buffered parser gzip failed");
        for (bool async : {false, true}) {
            Server server(response(encoded, chunked));
            http::AsioHttpClient::Options options;
            options.io_threads = 1;
            options.request_timeout = std::chrono::milliseconds(3000);
            options.max_response_bytes = 10000;
            http::AsioHttpClient client(options);
            std::string body;
            http::HttpStreamResult result;
            if (async) {
                http::AsioHttpClient::AwaitableStreamCallbacks callbacks;
                callbacks.on_chunk = [&body](std::string_view part) -> asio::awaitable<bool> { body.append(part); co_return true; };
                result = asio::co_spawn(client.executor(), client.coStreamGetWithOptions(server.url(), {}, {}, std::move(callbacks)), asio::use_future).get();
            } else {
                http::HttpStreamCallbacks callbacks;
                callbacks.on_chunk = [&body](std::string_view part) { body.append(part); return true; };
                result = client.streamGetWithOptions(server.url(), {}, {}, std::move(callbacks));
            }
            require(result.ok && result.bytes_received == original.size() && body == original, "streaming gzip differs between wire and decoded byte accounting");
        }
    }
    const auto truncated = response(encoded.substr(0, encoded.size() - 4), false);
    require(!detail::parseHttpResponse(truncated, 10000), "buffered parser accepted truncated gzip");
    for (bool async : {false, true}) {
        Server server(truncated);
        http::AsioHttpClient client;
        const auto result = async ? asio::co_spawn(client.executor(), client.coStreamGetWithOptions(server.url(), {}, {}, {}), asio::use_future).get() : client.streamGetWithOptions(server.url(), {}, {}, {});
        require(!result.ok && result.result_code == "invalid_content_encoding", "stream accepted incomplete gzip");
    }
}

} // namespace

int main()
{
    try {
        verifyDecoder();
        verifyClients();
        std::cout << "PASS: bounded gzip, checksum and truncation, buffered and both streaming paths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
