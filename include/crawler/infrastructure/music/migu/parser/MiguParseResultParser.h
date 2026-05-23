#pragma once

#include "crawler/application/ports/parsing/ParseResultParser.h"
#include "crawler/infrastructure/music/migu/parser/strategy/IParserStrategy.h"

#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 领域结果 parser，只负责策略式 ParseResult 解析。
class MiguParseResultParser : public application::ports::ParseResultParser {
public:
    MiguParseResultParser();

    // 解析 Migu 平台响应为领域结果。
    std::vector<domain::ParseResult> parse(const std::vector<char> &raw_data) const override;

private:
    std::vector<std::unique_ptr<strategy::IParserStrategy>> strategies_; // 领域结果解析策略
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
