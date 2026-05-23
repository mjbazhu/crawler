#pragma once

#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/infrastructure/http/server/HttpServer.h"

#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::http::server {

// Drogon 入站 HTTP server adapter，将 Drogon 请求转为 api 层模型并投递到受限 handler executor。
class DrogonHttpServer : public HttpServer {
public:
    // 构造函数：worker_threads 控制 Drogon 接入线程，handler_executor 执行 API handler。
    DrogonHttpServer(int worker_threads, std::shared_ptr<application::ports::TaskExecutor> handler_executor);
    ~DrogonHttpServer() override;

    // 绑定监听地址。
    void bind(const std::string &host, int port) override;

    // 启动 Drogon 事件循环。
    void run() override;

    // 停止 Drogon 事件循环。
    void stop() override;

    // 注册 API 路由。
    void route(const std::string &method, const std::string &path, api::routing::RequestHandler handler) override;

private:
    std::string host_ = "0.0.0.0";                  // 监听地址
    int port_ = 8080;                               // 监听端口
    int worker_threads_ = 1;                        // Drogon 接入线程数
    std::shared_ptr<application::ports::TaskExecutor> handler_executor_; // API handler 执行器
};

} // namespace orchestrator::modules::crawler::infrastructure::http::server
