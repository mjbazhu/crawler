#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace client = orchestrator::modules::crawler::infrastructure::http::client;
using namespace std::chrono_literals;
namespace {
// 不依赖 assert 或真实外部服务。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 请求观察只保存计数，不聚合大正文。
struct Observed {
    std::uint64_t bytes = 0;    // 逐片收到的量
    std::uint64_t declared = 0; // 请求头中的完整长度
};
// 非阻塞本机服务，析构仅停止自己线程，不遗留等待 accept/read 的任务。
class Receiver {
public:
    // stall 在读完正文后不响应，用来验证主动取消及超时。
    explicit Receiver(bool stall = false) : acceptor_(io_, {boost::asio::ip::make_address("127.0.0.1"), 0}), stall_(stall), result_(promise_.get_future())
    {
        port_ = acceptor_.local_endpoint().port();
        acceptor_.non_blocking(true);
        thread_ = std::thread([this] {
            try {
                promise_.set_value(serve());
            } catch (...) {
                promise_.set_exception(std::current_exception());
            }
        });
    }
    // 测试失败同样有界退出。
    ~Receiver()
    {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    // 独占回环端口。
    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_) + "/upload";
    }
    // 只在主线程取一次。
    Observed observed()
    {
        return result_.get();
    }
    std::atomic_bool body_complete {false}; // 并发测试屏障
private:
    // 每次最多 32 KiB，头部单独有上限。
    Observed serve()
    {
        boost::asio::ip::tcp::socket socket(io_);
        boost::system::error_code ec;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!stop_.load() && std::chrono::steady_clock::now() < deadline) {
            acceptor_.accept(socket, ec);
            if (!ec) break;
            if (ec != boost::asio::error::would_block && ec != boost::asio::error::try_again) throw std::runtime_error("loopback accept failed");
            std::this_thread::sleep_for(1ms);
        }
        if (!socket.is_open()) return {};
        socket.non_blocking(true);
        std::array<char, 32 * 1024> chunk {};
        std::string head;
        bool parsed = false;
        Observed value;
        const auto consume = [&](std::string_view bytes) {
            require(bytes.find_first_not_of('x') == std::string_view::npos, "stream body changed");
            value.bytes += bytes.size();
            require(value.bytes <= value.declared, "sent more than content length");
        };
        while (!stop_.load() && std::chrono::steady_clock::now() < deadline) {
            const auto count = socket.read_some(boost::asio::buffer(chunk), ec);
            if (count) {
                if (!parsed) {
                    head.append(chunk.data(), count);
                    const auto end = head.find("\r\n\r\n");
                    if (end != std::string::npos) {
                        const auto length = head.find("Content-Length: ");
                        require(length != std::string::npos && length < end && head.find("Connection: close\r\n") < end, "missing managed upload headers");
                        value.declared = std::stoull(head.substr(length + 16));
                        parsed = true;
                        consume(std::string_view(head).substr(end + 4));
                        head.clear();
                    } else
                        require(head.size() <= 65536, "unbounded upload header");
                } else
                    consume(std::string_view(chunk.data(), count));
                if (parsed && value.bytes == value.declared) {
                    body_complete.store(true);
                    if (!stall_) {
                        const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
                        boost::asio::write(socket, boost::asio::buffer(response), ec);
                        require(!ec, "loopback reply failed");
                        return value;
                    }
                }
            }
            if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset) return value;
            if (ec && ec != boost::asio::error::would_block && ec != boost::asio::error::try_again) return value;
            if (!count) std::this_thread::sleep_for(1ms);
        }
        return value;
    }
    boost::asio::io_context io_;              // 本机服务上下文
    boost::asio::ip::tcp::acceptor acceptor_; // 测试独占端口
    bool stall_ = false;                      // 是否延迟响应
    std::uint16_t port_ = 0;                  // 实际端口
    std::atomic_bool stop_ {false};           // 仅结束本服务
    std::promise<Observed> promise_;          // 传递线程错误
    std::future<Observed> result_;            // 主线程观察
    std::thread thread_;                      // 一个有界测试线程
};
// 按需生成大正文，单次供给不超过传输缓冲。
client::HttpUploadBody generated(std::uint64_t bytes, std::size_t &largest, int &reads)
{
    client::HttpUploadBody body;
    body.size_bytes = bytes;
    body.read = [left = bytes, &largest, &reads](std::span<char> chunk) mutable -> std::optional<std::size_t> {
        ++reads;
        largest = (std::max)(largest, chunk.size());
        const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(left, chunk.size()));
        std::memset(chunk.data(), 'x', count);
        left -= count;
        return count;
    };
    return body;
}
} // namespace

// 真实回环 TCP 验证有界大正文、单连接取消/超时和非阻塞限速。
int main()
{
    try {
        client::AsioHttpClient::Options configuration;
        configuration.io_threads = 1;
        client::AsioHttpClient http(configuration);
        const std::vector<std::string> headers {"Content-Type: application/octet-stream"};
        client::HttpRequestOptions options {5s, 1024};
        {
            Receiver server;
            std::size_t largest = 0;
            int reads = 0;
            constexpr auto size = 16 * 1024 * 1024 + 13;
            auto body = generated(size, largest, reads);
            auto lifetime = std::make_shared<int>(1);
            std::weak_ptr<int> released = lifetime;
            body.read = [read = std::move(body.read), lifetime = std::move(lifetime)](std::span<char> chunk) mutable { return read(chunk); };
            const auto value = http.streamPostWithOptions(server.url(), std::move(body), headers, options);
            body = {};
            require(value.response && value.response->status_code == 200 && value.request_started && value.bytes_sent == size, "large stream did not complete");
            require(released.expired(), "completed upload retained a source reader after return");
            require(server.observed().bytes == size && reads > 200 && largest <= 64 * 1024, "large body was aggregated or truncated");
        }
        {
            Receiver server(true);
            std::size_t largest = 0;
            int reads = 0;
            const auto started = std::chrono::steady_clock::now();
            const auto value = http.streamPostWithOptions(server.url(), generated(1024, largest, reads), headers, {200ms, 1024});
            require(!value.response && value.request_started && value.result_code == "timeout" && std::chrono::steady_clock::now() - started < 2s, "response stall bypassed upload timeout");
        }
        {
            Receiver slow(true), fast;
            std::atomic_bool cancelled {false};
            std::size_t slow_largest = 0, fast_largest = 0;
            int slow_reads = 0, fast_reads = 0;
            auto body = generated(1024, slow_largest, slow_reads);
            body.cancelled = [&] { return cancelled.load(); };
            auto pending = std::async(std::launch::async, [&] { return http.streamPostWithOptions(slow.url(), std::move(body), headers, options); });
            const auto until = std::chrono::steady_clock::now() + 2s;
            while (!slow.body_complete.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(1ms);
            require(slow.body_complete.load(), "slow request did not reach barrier");
            const auto healthy = http.streamPostWithOptions(fast.url(), generated(128 * 1024, fast_largest, fast_reads), headers, options);
            require(healthy.response && pending.wait_for(0ms) == std::future_status::timeout, "one stalled upload blocked another on one I/O thread");
            cancelled.store(true);
            const auto stopped = pending.get();
            require(!stopped.response && stopped.request_started && stopped.result_code == "cancelled", "in-flight upload did not actively cancel");
        }
        {
            Receiver server;
            std::size_t largest = 0;
            int reads = 0;
            auto body = generated(256 * 1024, largest, reads);
            body.bytes_per_second = 512 * 1024;
            const auto started = std::chrono::steady_clock::now();
            const auto value = http.streamPostWithOptions(server.url(), std::move(body), headers, options);
            require(value.response && std::chrono::steady_clock::now() - started >= 450ms, "per-request bandwidth limit was ignored");
        }
        {
            Receiver server;
            client::HttpUploadBody body;
            body.size_bytes = 1024;
            body.read = [](std::span<char>) -> std::optional<std::size_t> { return 0; };
            const auto value = http.streamPostWithOptions(server.url(), std::move(body), headers, options);
            require(!value.response && value.request_started && value.result_code == "upload_source_failed", "short upload source was accepted");
        }
        std::cout << "HttpStreamUploadTest passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "HttpStreamUploadTest failed: " << error.what() << '\n';
        return 1;
    }
}
