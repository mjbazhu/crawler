#include "crawler/application/use_cases/MiguPacmtokenService.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

constexpr std::size_t kMaxPacmtokenLength = 4096;

// 去掉字符串两端空白，避免把表单输入空格写入请求头。
std::string trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

// 判断 token 是否包含会污染 HTTP 头的换行字符。
bool containsHeaderBreak(std::string_view value)
{
    return value.find('\r') != std::string_view::npos || value.find('\n') != std::string_view::npos;
}

// 从完整 Cookie 文本中提取 pacmtoken，方便前端直接传浏览器 Cookie。
std::optional<std::string> extractPacmtokenFromCookie(std::string_view cookie)
{
    constexpr std::string_view kName = "pacmtoken";
    std::size_t start = 0;
    while (start < cookie.size()) {
        const std::size_t end = cookie.find(';', start);
        std::string part = trim(cookie.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        const std::size_t equal = part.find('=');
        if (equal != std::string::npos) {
            std::string name = trim(std::string_view(part).substr(0, equal));
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (name == kName) {
                return trim(std::string_view(part).substr(equal + 1));
            }
        }
        start = end == std::string_view::npos ? cookie.size() : end + 1;
    }
    return std::nullopt;
}

// 标准化前端输入，优先把完整 Cookie 规整成 pacmtoken 原值。
std::string normalizePacmtokenInput(std::string value)
{
    value = trim(value);
    if (auto cookie_token = extractPacmtokenFromCookie(value); cookie_token && !cookie_token->empty()) {
        return *cookie_token;
    }
    return value;
}

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 构造失败结果。
application::model::MiguPacmtokenRefreshResult failure(std::string error_code, std::string message, int upstream_status = 0)
{
    application::model::MiguPacmtokenRefreshResult result;
    result.success = false;
    result.error_code = std::move(error_code);
    result.message = std::move(message);
    result.upstream_status = upstream_status;
    result.updated_at = nowSeconds();
    return result;
}

} // namespace

MiguPacmtokenService::MiguPacmtokenService(
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> refresher,
    std::shared_ptr<application::ports::MiguPacmtokenStore> store
) : refresher_(std::move(refresher)),
    store_(std::move(store))
{}

application::model::MiguPacmtokenRefreshResult MiguPacmtokenService::refresh(application::model::MiguPacmtokenRefreshRequest request)
{
    std::string current_token = normalizePacmtokenInput(std::move(request.pacmtoken));
    if (current_token.empty() && store_) {
        if (auto stored = store_->currentPacmtoken()) {
            current_token = normalizePacmtokenInput(std::move(*stored));
        }
    }

    if (current_token.empty()) {
        return failure("bad_request", "pacmtoken is required");
    }
    if (current_token.size() > kMaxPacmtokenLength) {
        return failure("bad_request", "pacmtoken is too long");
    }
    if (containsHeaderBreak(current_token)) {
        return failure("bad_request", "pacmtoken contains invalid characters");
    }
    if (!refresher_) {
        return failure("not_configured", "migu pacmtoken refresher is not configured");
    }

    auto result = refresher_->refreshPacmtoken(current_token);
    if (!result.success) {
        if (result.error_code.empty()) {
            result.error_code = "upstream_failed";
        }
        if (result.message.empty()) {
            result.message = "migu pacmtoken refresh failed";
        }
        if (result.updated_at == 0) {
            result.updated_at = nowSeconds();
        }
        result.pacmtoken.clear();
        result.pacmtoken_length = 0;
        return result;
    }

    std::string refreshed_token = normalizePacmtokenInput(std::move(result.pacmtoken));
    if (refreshed_token.empty()) {
        refreshed_token = current_token;
    }
    if (refreshed_token.size() > kMaxPacmtokenLength || containsHeaderBreak(refreshed_token)) {
        return failure("upstream_invalid", "migu returned an invalid pacmtoken", result.upstream_status);
    }

    const bool changed = refreshed_token != current_token;
    result.success = true;
    result.refreshed = result.refreshed || changed;
    result.pacmtoken = std::move(refreshed_token);
    result.pacmtoken_length = result.pacmtoken.size();
    result.error_code.clear();
    if (result.message.empty()) {
        result.message = changed ? "migu pacmtoken refreshed" : "migu pacmtoken is still valid";
    }
    if (result.updated_at == 0) {
        result.updated_at = nowSeconds();
    }
    if (request.store && store_) {
        store_->updatePacmtoken(result.pacmtoken, result.updated_at);
    }
    return result;
}

application::model::MiguPacmtokenState MiguPacmtokenService::state() const
{
    application::model::MiguPacmtokenState state;
    state.configured = static_cast<bool>(store_);
    if (!store_) {
        return state;
    }

    const auto current = store_->currentPacmtoken();
    state.has_pacmtoken = current && !current->empty();
    state.pacmtoken_length = state.has_pacmtoken ? current->size() : 0;
    state.updated_at = store_->updatedAt();
    return state;
}

application::model::MiguPacmtokenState MiguPacmtokenService::current() const
{
    application::model::MiguPacmtokenState state = this->state();
    if (store_) {
        if (auto current = store_->currentPacmtoken(); current && !current->empty()) {
            state.pacmtoken = *current;
        }
    }
    return state;
}

} // namespace orchestrator::modules::crawler::application::use_cases
