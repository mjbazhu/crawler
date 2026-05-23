#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace orchestrator::modules::crawler::api::http {

// HTTP 请求抽象
struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> query_params;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

// HTTP 响应抽象
struct HttpResponse {
    int status_code;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    static HttpResponse json(int code, const std::string &json_body)
    {
        return {code, {{"Content-Type", "application/json"}}, json_body};
    }
};

} // namespace orchestrator::modules::crawler::api::http
