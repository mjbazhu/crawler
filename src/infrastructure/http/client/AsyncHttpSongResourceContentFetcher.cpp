#include "crawler/infrastructure/http/client/AsyncHttpSongResourceContentFetcher.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client {

namespace {

namespace asio = boost::asio;

// application bool 回调的异步结果，供 Asio 侧 timer 唤醒后读取。
struct ApplicationBoolCallbackState {
    std::mutex mutex;          // 保护 application 回调结果
    std::optional<bool> value; // 回调返回值
    std::exception_ptr error;  // 回调异常
};

// 持有响应头副本直到 application 回调完成，避免 const 引用跨协程悬空。
application::async::Task<bool> invokeResponseCallback(
    std::function<application::async::Task<bool>(const application::model::SongResourceContentStreamInfo &info)> callback,
    std::shared_ptr<const application::model::SongResourceContentStreamInfo> info
)
{
    co_return co_await callback(*info);
}

// 持有 body 分片副本直到 application sink 写入完成，避免 string_view/span 跨协程悬空。
application::async::Task<bool> invokeChunkCallback(
    std::function<application::async::Task<bool>(std::span<const char> chunk)> callback,
    std::shared_ptr<const std::string> chunk
)
{
    co_return co_await callback(std::span<const char>(chunk->data(), chunk->size()));
}

// 执行 application Task<bool>；使用普通协程函数避免捕获型 coroutine lambda 的闭包悬垂。
application::async::Task<void> runApplicationBoolTask(application::async::Task<bool> task, std::shared_ptr<ApplicationBoolCallbackState> state)
{
    try {
        const bool accepted = co_await task;
        std::lock_guard<std::mutex> lock(state->mutex);
        state->value.emplace(accepted);
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error = std::current_exception();
    }
}

// 将 application Task<bool> 桥接为 Boost.Asio awaitable，避免 HTTP I/O 协程阻塞等待下游 sink。
boost::asio::awaitable<bool> awaitApplicationBool(application::async::Task<bool> task, std::chrono::milliseconds timeout)
{
    auto executor = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(executor);
    auto state = std::make_shared<ApplicationBoolCallbackState>();
    timer->expires_after(timeout.count() > 0 ? timeout : std::chrono::milliseconds(30000));

    runApplicationBoolTask(std::move(task), state).startDetached([executor, timer, state](std::exception_ptr error) mutable {
        if (error) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = error;
        }
        asio::post(executor, [timer]() mutable {
            timer->cancel();
        });
    });

    boost::system::error_code ec;
    co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->error) {
        std::rethrow_exception(state->error);
    }
    co_return state->value.value_or(false);
}

// 映射底层流式 HTTP 结果为资源下载结果。
application::model::SongResourceContentStreamResult toResourceStreamResult(HttpStreamResult response)
{
    application::model::SongResourceContentStreamResult result;
    result.status_code = response.status_code;
    result.content_type = std::move(response.content_type);
    result.bytes_received = response.bytes_received;
    if (!response.ok) {
        if (response.result_code == "timeout") {
            result.error_category = "timeout";
        } else if (response.result_code == "stream_consumer_failed") {
            result.error_category = "persistence";
        } else if (response.result_code == "body_too_large" || response.result_code == "unsupported_content_encoding" || response.result_code == "invalid_content_encoding") {
            result.error_category = "resource";
        } else {
            result.error_category = "network";
        }
        result.result_code = response.result_code.empty() ? "network_failed" : std::move(response.result_code);
        result.message = response.message.empty() ? "resource stream download request failed" : std::move(response.message);
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
    result.result_code = "ok";
    return result;
}

} // namespace

AsyncHttpSongResourceContentFetcher::AsyncHttpSongResourceContentFetcher(std::shared_ptr<AsioHttpClient> client) : client_(std::move(client))
{}

application::async::Task<application::model::SongResourceContentStreamResult> AsyncHttpSongResourceContentFetcher::streamSongResourceContent(
    application::model::SongResourceContentFetchRequest request,
    application::ports::AsyncSongResourceContentStreamCallbacks callbacks
)
{
    application::model::SongResourceContentStreamResult failure;
    if (!client_ || request.url.empty()) {
        failure.error_category = "invalid_target";
        failure.result_code = "invalid_target";
        failure.message = "missing resource download target";
        co_return failure;
    }

    HttpRequestOptions options;
    if (request.timeout_ms > 0) {
        options.request_timeout = std::chrono::milliseconds(request.timeout_ms);
    }

    const auto callback_timeout = options.request_timeout.count() > 0 ? options.request_timeout : std::chrono::milliseconds(30000);
    AsioHttpClient::AwaitableStreamCallbacks http_callbacks;
    http_callbacks.on_response = [callbacks, callback_timeout](const HttpStreamInfo &info) mutable -> boost::asio::awaitable<bool> {
        if (!callbacks.on_response) {
            co_return true;
        }
        application::model::SongResourceContentStreamInfo resource_info;
        resource_info.status_code = info.status_code;
        resource_info.content_type = info.content_type;
        co_return co_await awaitApplicationBool(invokeResponseCallback(callbacks.on_response, std::make_shared<application::model::SongResourceContentStreamInfo>(std::move(resource_info))), callback_timeout);
    };
    http_callbacks.on_chunk = [callbacks, callback_timeout](std::string_view chunk) mutable -> boost::asio::awaitable<bool> {
        if (!callbacks.on_chunk) {
            co_return true;
        }
        co_return co_await awaitApplicationBool(invokeChunkCallback(callbacks.on_chunk, std::make_shared<std::string>(chunk)), callback_timeout);
    };

    application::async::TaskCompletionSource<application::model::SongResourceContentStreamResult> source;
    auto task = source.task();
    client_->spawnStreamGetWithOptions(
        std::move(request.url),
        std::move(request.request_headers),
        options,
        std::move(http_callbacks),
        [source](HttpStreamResult response) mutable {
            source.set_value(toResourceStreamResult(std::move(response)));
        }
    );
    co_return co_await task;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client
