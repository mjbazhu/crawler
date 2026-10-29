#include "migu/support/MetadataCoverTestSupport.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <iostream>
#include <fstream>
#include <thread>

namespace test = orchestrator::modules::crawler::tests::cover;
namespace model = test::model;

namespace {
// 可停止的回环 HTTP 服务；只服务本测试声明的响应，不使用固定端口。
class Server {
public:
    // 有序响应允许覆盖一次真实跳转与图片获取。
    explicit Server(std::vector<std::string> responses, int delay_ms = 0) : acceptor_(context_, {boost::asio::ip::make_address("127.0.0.1"), 0}), responses_(std::move(responses)), delay_ms_(delay_ms)
    {
        port_ = acceptor_.local_endpoint().port();
        acceptor_.non_blocking(true);
        worker_ = std::thread([this] {
            for (const auto &response : responses_) {
                boost::asio::ip::tcp::socket socket(context_);
                boost::system::error_code error;
                for (;;) {
                    if (stop_) return;
                    acceptor_.accept(socket, error);
                    if (!error) break;
                    if (error != boost::asio::error::would_block && error != boost::asio::error::try_again) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                boost::asio::streambuf request;
                boost::asio::read_until(socket, request, "\r\n\r\n", error);
                if (delay_ms_) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
                boost::asio::write(socket, boost::asio::buffer(response), error);
                socket.close(error);
            }
        });
    }
    // 停止并等待本测试服务线程。
    ~Server()
    {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }
    // 返回动态分配的回环 URL。
    std::string url() const
    {
        return "http://127.0.0.1:" + std::to_string(port_) + "/cover";
    }

private:
    boost::asio::io_context context_;         // 测试网络上下文
    boost::asio::ip::tcp::acceptor acceptor_; // 动态端口监听器
    std::vector<std::string> responses_;      // 有序响应
    int delay_ms_ = 0;                        // 响应延迟
    unsigned short port_ = 0;                 // 实际端口
    std::atomic_bool stop_ {false};           // 停止接受新连接
    std::thread worker_;                      // 服务线程
};
} // namespace

// 验证内容和回环传输边界，不请求真实平台。
int main()
{
    try {
        test::TempRoot root;
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto http = std::make_shared<test::Http>();
        test::files::MetadataCoverStore store(http, files, {"Accept: image/*", "X-Cover-Private: fixture"});
        model::MetadataCoverSaveRequest request;
        request.target = test::target(model::CoverOwner::Singer);
        request.target.claim_generation = 7;
        auto saved = store.save(request);
        test::require(saved.status == 2 && store.read(saved)->content_type == "image/png", "valid PNG was not saved with correct MIME");
        store.accept(saved.local_path);
        http->body = {'<', 'h', 't', 'm', 'l', '>'};
        test::require(store.save(request).result_code == "invalid_cover_content", "HTTP 200 error page was accepted as an image");
        http->body = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
        test::require(store.save(request).result_code == "invalid_cover_content", "truncated signature was accepted");
        http->status = 404;
        test::require(store.save(request).result_code == "http_404", "HTTP status classification was lost");
        http->status = 200;
        http->body.assign(8 * 1024 * 1024 + 1, 'x');
        test::require(store.save(request).result_code == "cover_too_large", "response limit was not enforced");
        http->body = test::png();
        http->response = [&](std::string_view url) -> std::optional<test::http::HttpResponse> {
            if (url == request.target.remote_url) return test::http::HttpResponse {302, {}, "", {{"Location", "https://cdn.example.test/image"}}};
            return test::http::HttpResponse {200, test::png(), "text/plain", {}};
        };
        auto redirected = store.save(request);
        test::require(redirected.status == 2 && store.read(redirected)->content_type == "image/png" && http->last_headers.size() == 1, "redirect or cross-origin header isolation failed");
        store.accept(redirected.local_path);
        http->response = [](std::string_view) -> std::optional<test::http::HttpResponse> { return test::http::HttpResponse {302, {}, "", {{"Location", "/same"}}}; };
        test::require(store.save(request).result_code == "cover_redirect_loop", "redirect loop was not bounded");
        http->response = [](std::string_view) -> std::optional<test::http::HttpResponse> { return test::http::HttpResponse {200, test::png(), "image/png", {{"Content-Length", "999"}}}; };
        test::require(store.save(request).result_code == "cover_incomplete", "incomplete payload was accepted");
        auto escaped = saved;
        escaped.local_path = "../outside.png";
        test::require(!store.available(escaped) && !store.read(escaped), "file reader accepted path outside its root");

        // 使用真实 Asio HTTP 客户端验证跳转、长度截断和超时。
        auto real_http = std::make_shared<test::http::AsioHttpClient>(test::http::AsioHttpClient::Options {});
        test::files::MetadataCoverStore real_store(real_http, files, {});
        const auto bytes = test::png();
        const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + std::to_string(bytes.size()) + "\r\nConnection: close\r\n\r\n" + std::string(bytes.begin(), bytes.end());
        {
            Server server({"HTTP/1.1 302 Found\r\nLocation: /image\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", response});
            request.target.remote_url = server.url();
            auto image = real_store.save(request);
            test::require(image.status == 2 && real_store.read(image).has_value(), "real HTTP redirect did not yield a readable cover");
            real_store.accept(image.local_path);
        }
        {
            Server server({"HTTP/1.1 200 OK\r\nContent-Length: 999\r\nConnection: close\r\n\r\n" + std::string(bytes.begin(), bytes.end())});
            request.target.remote_url = server.url();
            test::require(real_store.save(request).status == 3, "real truncated transfer was marked complete");
        }
        {
            Server server({response}, 250);
            request.target.remote_url = server.url();
            request.timeout_ms = 100;
            const auto timed_out = real_store.save(request);
            test::require(timed_out.status == 3 && timed_out.result_code == "timeout", "real timeout was not distinguished");
        }
        // 已提交文件同长损坏也必须被读取校验拒绝。
        std::fstream corrupted(files->getFullPath(saved.local_path), std::ios::binary | std::ios::in | std::ios::out);
        corrupted.seekp(35);
        corrupted.put('x');
        corrupted.close();
        test::require(!store.read(saved), "same-size corruption bypassed checksum verification");
        std::cout << "PASS: image validation, redirect policy, bounded reads, real HTTP truncation/timeout and local checksum verification\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
