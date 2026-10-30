#pragma once

#include <nlohmann/json_fwd.hpp>

namespace orchestrator::modules::crawler::api::controllers {

// 添加已实际注册的 v2 传输契约，旧同步协议保持独立。
// 批量只读状态契约，与执行接口隔离。
void appendResourceStatusOpenApi(nlohmann::json &document);
void appendResourceTransferOpenApi(nlohmann::json &document);

} // namespace orchestrator::modules::crawler::api::controllers
