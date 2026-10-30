#pragma once

#include "crawler/api/http/HttpModels.h"
#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/model/ResourceTransferQuery.h"
#include "crawler/application/result/ServiceResult.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::api::routing::transfer_api {
namespace model = application::model;
using Json = nlohmann::json;

// 验证有限 JSON 对象和已知键，拒绝忽略拼错的配置。
void keys(const Json &value, std::initializer_list<std::string_view> allowed);
// 解析有界 body；错误不回显请求文本。
Json body(const http::HttpRequest &request);
// 解析非零无符号 ID，拒绝小数和隐式类型转换。
std::uint64_t positive(const Json &value);
// 解析去重后的目标枚举，空数组按调用者语义解释。
std::vector<model::ResourceDestination> destinations(const Json &value);
// 解析同端完整预算；所有字段必须出现，更新不依赖有竞态的读取合并。
model::ResourceTransferJobPolicy policy(const Json &value);
// 解析查询字符串主键/游标，拒绝溢出或尾随字符。
std::uint64_t number(std::string_view value, bool zero = false);
// 序列化实际任务；不暴露来源、路径或凭据。
Json operation(const model::ResourceTransferOperation &value);
// 序列化逐平台事实，未知信息保持 null。
Json receipts(const model::ResourceCloudReceiptPage &value);
// 序列化配置中的安全预算字段。
Json policyJson(const model::ResourceTransferJobPolicy &value);
// 统一标准错误和 CORS，不使用原始异常消息。
http::HttpResponse error(application::result::ServiceErrorType type, const std::string &code = {});
// 返回带 CORS 的 JSON。
http::HttpResponse response(int status, const Json &value);
} // namespace orchestrator::modules::crawler::api::routing::transfer_api
