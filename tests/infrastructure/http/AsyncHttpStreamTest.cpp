#include "crawler/infrastructure/http/client/AsyncHttpSongResourceContentFetcher.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace http_client = crawler::infrastructure::http::client;

namespace {

// 测试断言失败时抛出明确错误，避免依赖 assert 宏。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 同步等待一个 application Task 完成。
template <typename T>
T runTask(app_async::Task<T> task)
{
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::optional<T> value;
    std::exception_ptr error;

    auto wrapper = [task = std::move(task), &mutex, &cv, &done, &value, &error]() mutable -> app_async::Task<void> {
        try {
            value.emplace(co_await task);
        } catch (...) {
            error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    };

    auto running = wrapper();
    running.start();

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&done]() {
        return done;
    });
    if (error) {
        std::rethrow_exception(error);
    }
    return std::move(*value);
}

// 单次响应本地 HTTP 服务，用于验证真实 Asio 流式读取。
class SingleResponseHttpServer {
public:
    explicit SingleResponseHttpServer(std::string response) : acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
                                                              response_(std::move(response))
    {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this]() {
            serveOnce();
        });
    }

    ~SingleResponseHttpServer()
    {
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        io_context_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_) + "/resource";
    }

private:
    void serveOnce()
    {
        try {
            boost::asio::ip::tcp::socket socket(io_context_);
            acceptor_.accept(socket);
            boost::asio::streambuf request_buffer;
            boost::system::error_code ignored;
            boost::asio::read_until(socket, request_buffer, "\r\n\r\n", ignored);
            boost::asio::write(socket, boost::asio::buffer(response_), ignored);
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            socket.close(ignored);
        } catch (...) {
        }
    }

    boost::asio::io_context io_context_;            // 服务端 I/O 上下文
    boost::asio::ip::tcp::acceptor acceptor_;       // 本地监听器
    std::string response_;                          // 固定 HTTP 响应
    unsigned short port_ = 0;                       // 动态端口
    std::thread thread_;                            // 服务线程
};

app_async::Task<bool> readyBool(bool value)
{
    co_return value;
}

model::SongResourceContentStreamResult fetchFromServer(std::string response, std::string &body, bool reject_chunk = false)
{
    SingleResponseHttpServer server(std::move(response));
    http_client::AsioHttpClient::Options options;
    options.io_threads = 1;
    options.request_timeout = std::chrono::milliseconds(3000);
    options.verify_tls = false;
    auto client = std::make_shared<http_client::AsioHttpClient>(options);
    http_client::AsyncHttpSongResourceContentFetcher fetcher(client);

    model::SongResourceContentFetchRequest request;
    request.url = server.url();
    request.timeout_ms = 3000;

    ports::AsyncSongResourceContentStreamCallbacks callbacks;
    callbacks.on_response = [](const model::SongResourceContentStreamInfo &info) {
        return readyBool(info.status_code == 200);
    };
    callbacks.on_chunk = [&body, reject_chunk](std::span<const char> chunk) {
        if (reject_chunk) {
            return readyBool(false);
        }
        body.append(chunk.data(), chunk.size());
        return readyBool(true);
    };

    return runTask(fetcher.streamSongResourceContent(std::move(request), std::move(callbacks)));
}

void testContentLengthBody()
{
    std::string body;
    const auto result = fetchFromServer("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello", body);
    require(result.ok, "Content-Length response should stream successfully");
    require(result.bytes_received == 5, "Content-Length byte count should be preserved");
    require(body == "hello", "Content-Length body should be delivered");
}

void testChunkedBody()
{
    std::string body;
    const auto result = fetchFromServer("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n", body);
    require(result.ok, "chunked response should stream successfully");
    require(result.bytes_received == 5, "chunked byte count should be decoded");
    require(body == "abcde", "chunked body should be decoded before delivery");
}

void testEofDelimitedBody()
{
    std::string body;
    const auto result = fetchFromServer("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nbye", body);
    require(result.ok, "EOF-delimited response should stream successfully");
    require(result.bytes_received == 3, "EOF-delimited byte count should be preserved");
    require(body == "bye", "EOF-delimited body should be delivered");
}

void testConsumerFailureMapping()
{
    std::string body;
    const auto result = fetchFromServer("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello", body, true);
    require(!result.ok, "consumer rejection should fail stream");
    require(result.error_category == "persistence", "consumer rejection should map to persistence");
    require(result.result_code == "stream_consumer_failed", "consumer rejection should expose stable result code");
}

void testHttp5xxMapping()
{
    std::string body;
    const auto result = fetchFromServer("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbusy", body);
    require(!result.ok, "HTTP 5xx should fail resource fetch");
    require(result.error_category == "resource", "HTTP 5xx should map to resource category");
    require(result.result_code == "http_status_503", "HTTP 5xx should preserve status in result code");
}

} // namespace

int main()
{
    try {
        testContentLengthBody();
        testChunkedBody();
        testEofDelimitedBody();
        testConsumerFailureMapping();
        testHttp5xxMapping();
        std::cout << "AsyncHttpStreamTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "AsyncHttpStreamTest failed: " << e.what() << std::endl;
        return 1;
    }
}
