#pragma once

#include "crawler/api/routing/RouteSink.h"

#include <string>

namespace orchestrator::modules::crawler::infrastructure::http::server {

class HttpServer : public api::routing::RouteSink {
public:
    virtual ~HttpServer() = default;

    // 配置监听地址与端口
    virtual void bind(const std::string &host, int port) = 0;

    // 启动服务器
    virtual void run() = 0;

    // 停止服务器
    virtual void stop() = 0;

    // route() 来自 api::routing::RouteSink，用于注册 API 路由。
};

} // namespace orchestrator::modules::crawler::infrastructure::http::server
