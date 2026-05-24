#include "crawler/infrastructure/http/server/DrogonHttpServer.h"

#include "crawler/infrastructure/observability/logging/Logger.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::server {

namespace {

namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;

// 记录 HTTP server adapter 运行日志，保持控制台和文件输出格式统一。
void logServer(logging::LogLevel level, std::string_view message)
{
    logging::Logger::instance().log(level, "DrogonHttpServer", message);
}

api::http::HttpRequest toApiRequest(const drogon::HttpRequestPtr &request)
{
    api::http::HttpRequest converted;
    converted.method = drogon::to_string(request->method());
    converted.path = request->getPath();

    for (const auto &[key, value] : request->getParameters()) {
        converted.query_params[key] = value;
    }
    for (const auto &[key, value] : request->getHeaders()) {
        converted.headers[key] = value;
    }

    const std::string_view body = request->getBody();
    converted.body.assign(body.data(), body.size());
    return converted;
}

drogon::HttpMethod toDrogonMethod(std::string_view method)
{
    if (method == "GET") {
        return drogon::Get;
    }
    if (method == "POST") {
        return drogon::Post;
    }
    if (method == "OPTIONS") {
        return drogon::Options;
    }
    if (method == "PUT") {
        return drogon::Put;
    }
    if (method == "DELETE") {
        return drogon::Delete;
    }
    if (method == "PATCH") {
        return drogon::Patch;
    }
    return drogon::Invalid;
}

drogon::HttpResponsePtr toDrogonResponse(api::http::HttpResponse response)
{
    auto converted = drogon::HttpResponse::newHttpResponse();
    converted->setStatusCode(static_cast<drogon::HttpStatusCode>(response.status_code));
    for (auto &[key, value] : response.headers) {
        converted->addHeader(std::move(key), std::move(value));
    }
    converted->setBody(std::move(response.body));
    return converted;
}

drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, std::string_view message)
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(status);
    response->addHeader("Content-Type", "application/json");
    response->setBody(std::string(R"({"error":")") + std::string(message) + R"("})");
    return response;
}

// 执行项目内 API handler；动态路由参数仍由 Drogon 匹配，项目内 handler 只依赖实际 path。
void dispatchApiHandler(
    const std::shared_ptr<application::ports::TaskExecutor> &handler_executor,
    api::routing::RequestHandler handler,
    const drogon::HttpRequestPtr &request,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback
)
{
    auto api_request = toApiRequest(request);
    auto response_callback = std::make_shared<std::function<void(const drogon::HttpResponsePtr &)>>(std::move(callback));
    auto dispatch = [handler = std::move(handler),
                     api_request = std::move(api_request),
                     response_callback]() mutable {
        try {
            (*response_callback)(toDrogonResponse(handler(std::move(api_request))));
        } catch (const std::exception &e) {
            logServer(logging::LogLevel::L_ERROR, std::string("Route handler exception: ") + e.what());
            (*response_callback)(jsonError(drogon::k500InternalServerError, "Internal Server Error"));
        } catch (...) {
            logServer(logging::LogLevel::L_ERROR, "Unknown route handler exception");
            (*response_callback)(jsonError(drogon::k500InternalServerError, "Internal Server Error"));
        }
    };

    if (!handler_executor) {
        dispatch();
        return;
    }

    try {
        handler_executor->submit(std::move(dispatch));
    } catch (const std::exception &e) {
        logServer(logging::LogLevel::L_ERROR, std::string("Submit route handler failed: ") + e.what());
        (*response_callback)(jsonError(drogon::k500InternalServerError, "Internal Server Error"));
    } catch (...) {
        logServer(logging::LogLevel::L_ERROR, "Submit route handler failed with unknown error");
        (*response_callback)(jsonError(drogon::k500InternalServerError, "Internal Server Error"));
    }
}

} // namespace

DrogonHttpServer::DrogonHttpServer(int worker_threads, std::shared_ptr<application::ports::TaskExecutor> handler_executor) : worker_threads_((std::max)(1, worker_threads)),
                                                                                                                             handler_executor_(std::move(handler_executor))
{}

DrogonHttpServer::~DrogonHttpServer() = default;

void DrogonHttpServer::bind(const std::string &host, int port)
{
    host_ = host.empty() ? "0.0.0.0" : host;
    port_ = port;
}

void DrogonHttpServer::run()
{
    auto &framework = drogon::app();
    framework.setThreadNum(static_cast<size_t>(worker_threads_));
    framework.addListener(host_, port_);
    framework.setCustom404Page(jsonError(drogon::k404NotFound, "Not Found"), false);
    framework.setExceptionHandler([](const std::exception &e,
                                     const drogon::HttpRequestPtr &,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
        logServer(logging::LogLevel::L_ERROR, std::string("Handler exception: ") + e.what());
        callback(jsonError(drogon::k500InternalServerError, "Internal Server Error"));
    });

    logServer(logging::LogLevel::L_INFO, "Listening on http://" + host_ + ':' + std::to_string(port_));
    framework.run();
}

void DrogonHttpServer::stop()
{
    drogon::app().quit();
}

void DrogonHttpServer::route(const std::string &method, const std::string &path, api::routing::RequestHandler handler)
{
    const auto drogon_method = toDrogonMethod(method);
    if (drogon_method == drogon::Invalid) {
        throw std::invalid_argument("unsupported HTTP method: " + method);
    }

    if (path.find("{1}") != std::string::npos) {
        drogon::app().registerHandler(
            path,
            [this, handler = std::move(handler)](
                const drogon::HttpRequestPtr &request,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                const std::string &
            ) mutable {
                dispatchApiHandler(handler_executor_, handler, request, std::move(callback));
            },
            {drogon_method}
        );
        return;
    }

    drogon::app().registerHandler(
        path,
        [this, handler = std::move(handler)](
            const drogon::HttpRequestPtr &request,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback
        ) mutable {
            dispatchApiHandler(handler_executor_, handler, request, std::move(callback));
        },
        {drogon_method}
    );
}

} // namespace orchestrator::modules::crawler::infrastructure::http::server
