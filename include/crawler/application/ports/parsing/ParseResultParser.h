#pragma once

#include "crawler/domain/music/model/ParseResult.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 领域解析结果 parser port，只把原始响应体转换为领域解析结果。
class ParseResultParser {
public:
    virtual ~ParseResultParser() = default;

    // 解析 HTTP 响应体为领域解析结果，要求实现保持无副作用。
    virtual std::vector<domain::ParseResult> parse(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
