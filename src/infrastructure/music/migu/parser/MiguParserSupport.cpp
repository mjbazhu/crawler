#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/strategy/MiguStrategies.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

std::vector<domain::ParseResult> parseResultsWithStrategies(
    const std::vector<char> &raw_data,
    const std::vector<std::unique_ptr<strategy::IParserStrategy>> &strategies
)
{
    std::vector<domain::ParseResult> results;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return results;
    }
    const auto &root = *root_opt;

    for (const auto &strategy : strategies) {
        if (strategy->parse(root, results)) {
            // 策略匹配后立即返回，避免歌手歌曲接口中的非歌曲卡片被后续宽松策略误解析。
            return results;
        }
    }

    return results;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
