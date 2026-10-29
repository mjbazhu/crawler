#pragma once

#include <nlohmann/json_fwd.hpp>

namespace orchestrator::modules::crawler::api::controllers {

// 补入独立封面路径和 schema，保持主工厂只承担文档组合。
void appendMetadataCoverOpenApi(nlohmann::json &document);

} // namespace orchestrator::modules::crawler::api::controllers
