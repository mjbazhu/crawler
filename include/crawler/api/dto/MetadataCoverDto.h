#pragma once

#include "crawler/api/http/HttpModels.h"
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/use_cases/MetadataCoverService.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>

namespace orchestrator::modules::crawler::api::dto {

// 独立封面请求 DTO，未知字段和错误类型均拒绝。
struct MetadataCoverRequestDto {
    application::model::MetadataCoverOptions options; // 已验证的下载筛选及策略
    bool async = true;                                // 默认创建后台任务
    int stale_after_seconds = 1800;                   // 恢复超时阈值
    // 解析严格的 JSON 对象，不接受 URL 或文件路径。
    static std::optional<MetadataCoverRequestDto> fromRequest(const http::HttpRequest &request);
};

// 解析 GET 队列的受限数字筛选和所有者类型。
std::optional<application::model::MetadataCoverQuery> metadataCoverQuery(const http::HttpRequest &request);
// 封面 DTO 只输出身份、相对路径与脱敏回执，本地 URL 根据路由生成。
nlohmann::json metadataCoverToJson(const application::model::MetadataCoverRecord &record);
// 一次批量查询为目录页补本地封面；未装配兼容后端时返回 cover=null。
bool attachMetadataCovers(nlohmann::json &body, application::model::CoverOwner owner, const std::shared_ptr<application::use_cases::MetadataCoverService> &service);

} // namespace orchestrator::modules::crawler::api::dto
