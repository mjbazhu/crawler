#pragma once

#include "crawler/api/http/HttpModels.h"

#include <functional>
#include <string>

namespace orchestrator::modules::crawler::api::routing {

// API 路由处理函数；HTTP server adapter 负责选择具体执行线程。
using RequestHandler = std::function<api::http::HttpResponse(api::http::HttpRequest)>;

// 路由注册端口，由具体 HTTP server adapter 实现。
class RouteSink {
public:
    virtual ~RouteSink() = default;

    // 注册一个方法和路径对应的处理函数。
    virtual void route(const std::string &method, const std::string &path, RequestHandler handler) = 0;
};

} // namespace orchestrator::modules::crawler::api::routing
