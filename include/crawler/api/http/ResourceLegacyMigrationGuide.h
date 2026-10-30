#pragma once
#include "crawler/api/http/HttpModels.h"
#include <string_view>
namespace orchestrator::modules::crawler::api::http {
// parallel拒绝旧writer时给出固定迁移契约，不执行旧处理器或转换成隐式传输。
HttpResponse resourceLegacyMigrationResponse(std::string_view path);
} // namespace orchestrator::modules::crawler::api::http
