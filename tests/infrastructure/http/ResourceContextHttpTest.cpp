#include "crawler/infrastructure/http/client/ResourceContextHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace client = orchestrator::modules::crawler::infrastructure::http::client;
namespace model = orchestrator::modules::crawler::application::model;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 本机非阻塞服务，失败时也能停止 accept/read，不访问外部平台。
class Receiver {
public:
    explicit Receiver(bool reply) : acceptor_(io_, {boost::asio::ip::make_address("127.0.0.1"), 0}), port_(acceptor_.local_endpoint().port())
    {
        acceptor_.non_blocking(true);
        thread_ = std::jthread([this, reply](std::stop_token stop) {
            boost::asio::ip::tcp::socket socket(io_);
            boost::system::error_code error;
            while (!stop.stop_requested()) {
                acceptor_.accept(socket, error);
                if (!error) break;
                std::this_thread::sleep_for(1ms);
            }
            if (!socket.is_open()) return;
            socket.non_blocking(true);
            std::array<char, 2048> buffer {};
            std::string head;
            while (!stop.stop_requested()) {
                const auto count = socket.read_some(boost::asio::buffer(buffer), error);
                if (count) head.append(buffer.data(), count);
                if (head.find("\r\n\r\n") != std::string::npos) {
                    received.store(true);
                    if (reply) {
                        const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
                        boost::asio::write(socket, boost::asio::buffer(response), error);
                        return;
                    }
                }
                if (error && error != boost::asio::error::would_block && error != boost::asio::error::try_again) return;
                std::this_thread::sleep_for(1ms);
            }
        });
    }
    ~Receiver()
    {
        thread_.request_stop();
    }
    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_) + "/source";
    }
    std::atomic_bool received {false}; // 测试请求已到服务端屏障
private:
    boost::asio::io_context io_;              // 单测试独立网络资源
    boost::asio::ip::tcp::acceptor acceptor_; // 只绑定回环随机端口
    unsigned short port_;                     // 不输出完整请求
    std::jthread thread_;                     // 析构有界停止
};
} // namespace

int main()
{
    try {
        client::AsioHttpClient::Options options;
        options.io_threads = 1;
        options.request_timeout = 5s;
        auto http = std::make_shared<client::AsioHttpClient>(options);
        const auto caller = std::this_thread::get_id();
        std::atomic_int caller_checks {0};
        client::ResourceContextHttpClient cached(http, {std::chrono::steady_clock::now() + 5s, [&] { if (std::this_thread::get_id() == caller) ++caller_checks; return false; }});
        const auto cached_context = cached.executionContext();
        for (int index = 0; index < 100; ++index) require(!cached_context.stopped(), "cached cancellation unexpectedly stopped");
        require(caller_checks == 0, "cached chunk context invoked persistence cancellation probe");
        for (const bool streaming : {false, true}) {
            Receiver stalled(false), healthy(true);
            std::atomic_bool cancelled {false};
            model::ResourceTransferContext context {std::chrono::steady_clock::now() + 5s, [&] { return cancelled.load(); }};
            client::ResourceContextHttpClient bounded(http, context);
            auto waiting = std::async(std::launch::async, [&] {
                if (streaming) return bounded.streamGetWithOptions(stalled.url(), {}, {}, {}).ok;
                return bounded.get(stalled.url()).has_value();
            });
            const auto until = std::chrono::steady_clock::now() + 1s;
            while (!stalled.received.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(1ms);
            require(stalled.received.load(), "source did not reach HTTP server");
            require(http->get(healthy.url()).has_value(), "one stalled source occupied shared HTTP executor");
            cancelled.store(true);
            require(waiting.wait_for(1s) == std::future_status::ready && !waiting.get(), "source cancellation waited for unrelated full request timeout");
        }
        Receiver stalled(false);
        model::ResourceTransferContext short_context {std::chrono::steady_clock::now() + 120ms, {}};
        client::ResourceContextHttpClient bounded(http, short_context);
        const auto begin = std::chrono::steady_clock::now();
        require(!bounded.get(stalled.url()) && std::chrono::steady_clock::now() - begin < 1s, "source request ignored remaining item deadline");
        std::cout << "resource context HTTP: independent request, active GET/stream cancellation and remaining deadline passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "resource context HTTP test failed: " << error.what() << '\n';
        return 1;
    }
}
