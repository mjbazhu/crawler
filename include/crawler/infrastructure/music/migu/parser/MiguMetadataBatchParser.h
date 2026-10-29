#pragma once
#include "crawler/application/ports/parsing/MetadataBatchParser.h"
#include "crawler/application/result/ServiceResult.h"

#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// 无共享可变状态，可并发解析多个响应。
class MiguMetadataBatchParser final : public application::ports::MetadataBatchParser {
public:
    // 共用各实体 parser 的字段提取函数，只做一次 JSON 解码。
    application::model::MetadataWriteBatch parseMetadataBatch(const std::vector<char> &body) const override;
    // 保留业务拒绝与临时无效响应的错误类型，供调用方决定是否重试。
    application::result::ServiceResult<application::model::MetadataWriteBatch> parseMetadataResponse(const std::vector<char> &body) const override;
};
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
