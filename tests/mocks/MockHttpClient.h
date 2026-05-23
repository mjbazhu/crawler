#pragma once

#include "crawler/infrastructure/http/client/HttpClient.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client {

class MockHttpClient : public HttpClient {
public:
    // URL -> Response 映射
    std::unordered_map<std::string, HttpResponse> mock_responses;
    std::string last_request_url;
    std::vector<std::string> last_request_headers;

    std::optional<HttpResponse> get(std::string_view url, const std::vector<std::string> &headers = {}) override
    {
        std::string url_str(url);
        last_request_url = url_str;
        last_request_headers = headers;
        if (mock_responses.count(url_str)) {
            return mock_responses[url_str];
        }
        return std::nullopt;
    }

    std::optional<HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers = {}) override
    {
        return std::nullopt; // 暂不使用
    }

    AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) override
    {
        co_return get(url, headers);
    }

    AwaitableResponse coPost(std::string url, std::string body, std::vector<std::string> headers = {}) override
    {
        co_return post(url, body, headers);
    }
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client
