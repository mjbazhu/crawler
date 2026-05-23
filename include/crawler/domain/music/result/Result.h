#pragma once
#include <variant>
#include <string>
#include <stdexcept>

namespace orchestrator::modules::crawler::domain::result {

enum class ErrorCode {
    Success = 0,
    NetworkError,
    DatabaseError,
    ParseError,
    IOError,
    NotFound
};

// 简易 Result 模板
// 设计意图：强制调用方检查返回值，避免异常在模块间隐式传播
template <typename T>
struct Result {
    std::variant<T, ErrorCode> value;
    std::string errorMessage;

    // 检查是否成功
    bool is_ok() const
    {
        return std::holds_alternative<T>(value);
    }

    // 获取错误码
    ErrorCode error() const
    {
        if (is_ok()) return ErrorCode::Success;
        return std::get<ErrorCode>(value);
    }

    // 解包数据，若失败则抛出异常（仅用于确信成功的场景）
    T unwrap()
    {
        if (auto *val = std::get_if<T>(&value)) {
            return *val;
        }
        throw std::runtime_error("Attempted to unwrap failed Result: " + errorMessage);
    }

    static Result<T> ok(T v)
    {
        return Result<T> {v, ""};
    }
    static Result<T> fail(ErrorCode c, std::string msg)
    {
        return Result<T> {c, std::move(msg)};
    }
};

// 特化 void 版本，用于只关注成功/失败的操作
template <>
struct Result<void> {
    ErrorCode code;
    std::string errorMessage;

    bool is_ok() const
    {
        return code == ErrorCode::Success;
    }

    static Result<void> ok()
    {
        return {ErrorCode::Success, ""};
    }
    static Result<void> fail(ErrorCode c, std::string msg)
    {
        return {c, std::move(msg)};
    }
};
} // namespace orchestrator::modules::crawler::domain::result
