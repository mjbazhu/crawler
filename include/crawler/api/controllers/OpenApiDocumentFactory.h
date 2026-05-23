#pragma once

#include <string>

namespace orchestrator::modules::crawler::api::controllers {

// OpenAPI 文档工厂，集中生成 Crawler API 文档，避免 controller 承担文档拼装职责。
class OpenApiDocumentFactory {
public:
    // 生成 Migu API 的 OpenAPI JSON 文档。
    static std::string miguDocumentJson();
};

} // namespace orchestrator::modules::crawler::api::controllers
