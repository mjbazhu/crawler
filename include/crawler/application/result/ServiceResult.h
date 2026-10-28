#pragma once

#include <string>
#include <optional>
#include <variant>
#include <utility>

namespace orchestrator::modules::crawler::application::result {

// 业务错误枚举（不直接对应 HTTP Code）
enum class ServiceErrorType {
    None,             // 成功
    InvalidParam,     // 参数错误 (对应 400)
    NotFound,         // 资源未找到 (对应 404)
    CommitUncertain,  // 提交响应丢失，需按实际存储核对
    Conflict,         // 状态冲突 (对应 409)
    ExternalApiError, // 外部依赖失败 (对应 502/500)
    InternalError     // 内部错误 (对应 500)
};

// 业务层通用返回结果
// T: 成功时的数据类型
template <typename T>
class ServiceResult {
public:
    // 成功构造
    static ServiceResult<T> success(T data)
    {
        return ServiceResult(std::move(data), ServiceErrorType::None, "");
    }

    // 失败构造
    static ServiceResult<T> failure(ServiceErrorType type, std::string msg)
    {
        return ServiceResult(std::nullopt, type, std::move(msg));
    }

    bool isSuccess() const
    {
        return error_type_ == ServiceErrorType::None;
    }
    ServiceErrorType error() const
    {
        return error_type_;
    }
    const std::string &message() const
    {
        return message_;
    }

    // 成功时读取结果，失败时为空，避免基础设施通过额外回调传递提交结果。
    const T *data() const
    {
        return data_ ? &*data_ : nullptr;
    }

private:
    ServiceResult(std::optional<T> data, ServiceErrorType type, std::string msg) : data_(std::move(data)), error_type_(type), message_(std::move(msg))
    {}

    std::optional<T> data_;
    ServiceErrorType error_type_;
    std::string message_;
};

} // namespace orchestrator::modules::crawler::application::result
