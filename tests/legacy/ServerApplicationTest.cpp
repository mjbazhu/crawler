#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <future>
#include <filesystem>

namespace crawler = orchestrator::modules::crawler;
using crawler::infrastructure::app::ServerApplication;

void test_server_lifecycle()
{
    std::cout << "Running ServerApplication Lifecycle Test..." << std::endl;

    // 1. 配置
    ServerApplication::Config config;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.db_path = "test_server_lifecycle.db";
    config.storage_path = "test_data";

    // 清理测试环境
    if (std::filesystem::exists(config.db_path)) std::filesystem::remove(config.db_path);
    if (std::filesystem::exists(config.storage_path)) std::filesystem::remove_all(config.storage_path);

    ServerApplication app(config);
    assert(app.initialize() == true);

    // 2. 在独立线程中启动服务器
    std::promise<void> start_signal;
    std::thread server_thread([&app, &start_signal]() {
        start_signal.set_value();
        app.run();
    });

    // 等待启动
    start_signal.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 给一点时间让 socket bind 完成

    // 3. 发送请求验证服务器是否存活
    crawler::infrastructure::http::client::AsioHttpClient client;
    std::string url = "http://127.0.0.1:8080/api/migu/crawl"; // 这是一个存在的路由

    // 发送一个空的 POST 请求，预期返回 400 (Bad Request) 或 JSON 解析错误，但只要有响应就说明服务器在运行
    auto resp = client.post(url, "{}");

    if (resp) {
        std::cout << "[Pass] Server responded with status: " << resp->status_code << std::endl;
        // 只要能连上，不管是 200, 400 还是 500，都说明 ServerApplication 启动成功了
        assert(resp->status_code > 0);
    } else {
        std::cerr << "[Fail] Server did not respond." << std::endl;
        assert(false);
    }

    // 4. 停止服务器
    std::cout << "Stopping server..." << std::endl;
    app.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    // 清理
    if (std::filesystem::exists(config.db_path)) std::filesystem::remove(config.db_path);
    if (std::filesystem::exists(config.storage_path)) std::filesystem::remove_all(config.storage_path);
}

int main()
{
    test_server_lifecycle();
    return 0;
}
