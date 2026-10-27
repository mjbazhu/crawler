#pragma once
#include "crawler/application/ports/parsing/MetadataBatchParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// 无共享可变状态，可并发解析多个响应。
class MiguMetadataBatchParser final : public application::ports::MetadataBatchParser {
public:
    // 共用各实体 parser 的字段提取函数，只做一次 JSON 解码。
    application::model::MetadataWriteBatch parseMetadataBatch(const std::vector<char> &body) const override;
};
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
