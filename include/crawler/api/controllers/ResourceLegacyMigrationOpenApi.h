#pragma once
#include <nlohmann/json.hpp>
namespace orchestrator::modules::crawler::api::controllers {
// 为不等价的旧writer迁移提示补充可选错误字段，保留成功契约。
void appendResourceLegacyMigrationOpenApi(nlohmann::json &document);
} // namespace orchestrator::modules::crawler::api::controllers
