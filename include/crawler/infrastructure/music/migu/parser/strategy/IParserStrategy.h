#pragma once

#include "crawler/domain/music/model/ParseResult.h"

#include <nlohmann/json.hpp>

#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {
namespace strategy {

/**
 * @brief 解析策略接口
 * 针对特定的 JSON 结构提取元数据
 */
class IParserStrategy {
public:
    virtual ~IParserStrategy() = default;

    /**
     * @brief 尝试解析 JSON 数据
     * @param root 已解析的 JSON 根对象
     * @param[out] results 解析结果输出容器
     * @return true 表示该策略成功匹配并解析了数据；false 表示格式不匹配
     */
    virtual bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const = 0;
};

}
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy
