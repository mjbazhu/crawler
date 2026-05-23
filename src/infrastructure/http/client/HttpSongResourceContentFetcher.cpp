#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client {

HttpSongResourceContentFetcher::HttpSongResourceContentFetcher(std::shared_ptr<HttpClient> client) : client_(std::move(client))
{}

application::model::SongResourceContentFetchResult HttpSongResourceContentFetcher::fetchSongResourceContent(application::model::SongResourceContentFetchRequest request)
{
    application::model::SongResourceContentFetchResult result;
    if (!client_ || request.url.empty()) {
        result.error_category = "invalid_target";
        result.result_code = "invalid_target";
        result.message = "missing resource download target";
        return result;
    }

    HttpRequestOptions options;
    if (request.timeout_ms > 0) {
        options.request_timeout = std::chrono::milliseconds(request.timeout_ms);
    }
    auto response = client_->getWithOptions(request.url, request.request_headers, options);
    if (!response) {
        result.error_category = "network";
        result.result_code = "network_failed";
        result.message = "resource download request failed";
        return result;
    }

    result.status_code = response->status_code;
    result.content_type = response->content_type;
    result.body = std::move(response->body);
    if (result.status_code < 200 || result.status_code >= 300) {
        result.error_category = "resource";
        result.result_code = "http_status_" + std::to_string(result.status_code);
        result.message = "resource download returned non-success status";
        return result;
    }
    if (result.body.empty()) {
        result.error_category = "resource";
        result.result_code = "empty_body";
        result.message = "resource download returned empty body";
        return result;
    }

    result.ok = true;
    result.error_category.clear();
    result.result_code = "ok";
    result.message.clear();
    return result;
}

application::model::SongResourceContentStreamResult HttpSongResourceContentFetcher::streamSongResourceContent(application::model::SongResourceContentFetchRequest request, application::model::SongResourceContentStreamCallbacks callbacks)
{
    application::model::SongResourceContentStreamResult result;
    if (!client_ || request.url.empty()) {
        result.error_category = "invalid_target";
        result.result_code = "invalid_target";
        result.message = "missing resource download target";
        return result;
    }

    HttpRequestOptions options;
    if (request.timeout_ms > 0) {
        options.request_timeout = std::chrono::milliseconds(request.timeout_ms);
    }

    application::model::SongResourceContentStreamInfo stream_info;
    HttpStreamCallbacks http_callbacks;
    http_callbacks.on_response = [&callbacks, &stream_info](const HttpStreamInfo &info) {
        stream_info.status_code = info.status_code;
        stream_info.content_type = info.content_type;
        if (callbacks.on_response) {
            return callbacks.on_response(stream_info);
        }
        return true;
    };
    http_callbacks.on_chunk = [&callbacks](std::string_view chunk) {
        if (callbacks.on_chunk) {
            return callbacks.on_chunk(chunk);
        }
        return true;
    };

    auto response = client_->streamGetWithOptions(request.url, request.request_headers, options, std::move(http_callbacks));
    result.status_code = response.status_code;
    result.content_type = response.content_type;
    result.bytes_received = response.bytes_received;
    if (!response.ok) {
        if (response.result_code == "timeout") {
            result.error_category = "timeout";
        } else if (response.result_code == "stream_consumer_failed") {
            result.error_category = "persistence";
        } else if (response.result_code == "body_too_large" || response.result_code == "unsupported_content_encoding") {
            result.error_category = "resource";
        } else {
            result.error_category = "network";
        }
        result.result_code = response.result_code.empty() ? "network_failed" : response.result_code;
        result.message = response.message.empty() ? "resource stream download request failed" : response.message;
        return result;
    }

    if (result.status_code < 200 || result.status_code >= 300) {
        result.error_category = "resource";
        result.result_code = "http_status_" + std::to_string(result.status_code);
        result.message = "resource stream download returned non-success status";
        return result;
    }
    if (result.bytes_received == 0) {
        result.error_category = "resource";
        result.result_code = "empty_body";
        result.message = "resource stream download returned empty body";
        return result;
    }

    result.ok = true;
    result.error_category.clear();
    result.result_code = "ok";
    result.message.clear();
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client
