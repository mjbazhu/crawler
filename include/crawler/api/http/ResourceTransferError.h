#pragma once

#include "crawler/api/http/HttpModels.h"
#include "crawler/application/result/ServiceResult.h"

#include <string_view>

namespace orchestrator::modules::crawler::api::http {
// 纯函数，可并发调用；仅输出已知稳定错误码，不回显仓储或请求细节。
HttpResponse resourceTransferError(application::result::ServiceErrorType type, std::string_view code = {});
} // namespace orchestrator::modules::crawler::api::http
