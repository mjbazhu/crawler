#include "crawler/infrastructure/http/server/DrogonHttpServer.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/execution/asio/AsioExecutionContext.h"
#include <boost/asio/awaitable.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <future>
#include <string>
#include <atomic>

namespace crawler = orchestrator::modules::crawler;
namespace crawler_api = crawler::api;
using crawler::infrastructure::http::client::AsioHttpClient;
using crawler::infrastructure::http::server::DrogonHttpServer;

void test_http_server_integration()
{
    std::cout << "Running HttpServer Integration Test..." << std::endl;

    // 1. 配置服务器
    int port = 8090;
    crawler::infrastructure::execution::asio::AsioExecutionContext handler_context(4);
    DrogonHttpServer server(4, handler_context.executor()); // 4个 Drogon 线程
    server.bind("127.0.0.1", port);

    // 注册 GET 路由
    server.route("GET", "/api/test", [](crawler_api::http::HttpRequest req) -> boost::asio::awaitable<crawler_api::http::HttpResponse> {
        co_return crawler_api::http::HttpResponse::json(200, "{\"status\": \"ok\", \"message\": \"GET success\"}");
    });

    // 注册 POST 路由 (Echo)
    server.route("POST", "/api/echo", [](crawler_api::http::HttpRequest req) -> boost::asio::awaitable<crawler_api::http::HttpResponse> {
        // 简单回显请求体
        co_return crawler_api::http::HttpResponse::json(200, req.body);
    });

    // 2. 在独立线程中启动服务器
    std::promise<void> start_signal;
    std::thread server_thread([&server, &start_signal]() {
        // 通知主线程即将启动
        start_signal.set_value();
        server.run();
    });

    // 等待服务器启动 (简单的延时，确保 socket bind/listen 完成)
    start_signal.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 3. 初始化客户端
    AsioHttpClient client;
    std::string base_url = "http://127.0.0.1:" + std::to_string(port);

    // 4. 执行测试用例

    // Case A: 测试 GET 请求
    {
        std::cout << "[Test] GET /api/test ... ";
        auto resp = client.get(base_url + "/api/test");
        assert(resp.has_value());
        assert(resp->status_code == 200);

        std::string body(resp->body.begin(), resp->body.end());
        assert(body.find("GET success") != std::string::npos);
        std::cout << "Passed" << std::endl;
    }

    // Case B: 测试 POST 请求
    {
        std::cout << "[Test] POST /api/echo ... ";
        std::string payload = "{\"data\": 12345}";
        auto resp = client.post(base_url + "/api/echo", payload);
        assert(resp.has_value());
        assert(resp->status_code == 200);

        std::string body(resp->body.begin(), resp->body.end());
        assert(body == payload);
        std::cout << "Passed" << std::endl;
    }

    // Case C: 测试 404 Not Found
    {
        std::cout << "[Test] GET /api/unknown ... ";
        auto resp = client.get(base_url + "/api/unknown");
        assert(resp.has_value());
        assert(resp->status_code == 404);
        std::cout << "Passed" << std::endl;
    }

    // Case D: 并发测试 (使用多个线程同时请求)
    {
        std::cout << "[Test] Concurrent Requests ... ";
        std::vector<std::thread> threads;
        int concurrency = 10;
        std::atomic<int> success_count {0};

        for (int i = 0; i < concurrency; ++i) {
            threads.emplace_back([&client, base_url, &success_count]() {
                auto resp = client.get(base_url + "/api/test");
                if (resp && resp->status_code == 200) {
                    success_count++;
                }
            });
        }

        for (auto &t : threads) {
            t.join();
        }

        assert(success_count == concurrency);
        std::cout << "Passed (" << success_count << "/" << concurrency << ")" << std::endl;
    }

    // 5. 停止服务器
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    std::cout << "HttpServer Integration Test Completed Successfully." << std::endl;
}

int main()
{
    test_http_server_integration();
    return 0;
}
