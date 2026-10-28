#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

std::optional<application::model::SingerSourceMetadata> parseSingerSourceFromItem(
    const nlohmann::json &item,
    const domain::CountryGender &singer_class,
    std::string_view tab,
    std::uint32_t index_order
)
{
    if (JsonUtils::getString(item, "view") != "ZJ-Singer-Item") {
        return std::nullopt;
    }

    const std::string source_view_id = JsonUtils::getString(item, "viewId");
    const std::string source_action = JsonUtils::getString(item, "action");
    const std::string source_singer_id = firstNonEmpty({
        JsonUtils::getString(item, "resId"),
        JsonUtils::getString(item, "txt2"),
        extractActionId(source_action),
        extractViewIdSuffix(source_view_id),
    });
    const std::string singer_name = JsonUtils::getString(item, "txt");
    if (source_singer_id.empty() || singer_name.empty()) {
        return std::nullopt;
    }

    application::model::SingerSourceMetadata metadata;
    metadata.source = "migu";
    metadata.name = singer_name;
    metadata.normalized_name = singer_name;
    metadata.gender = singer_class.gender;
    metadata.source_singer_id = source_singer_id;
    metadata.source_tag = JsonUtils::getString(item, "txt3");
    metadata.source_metric = JsonUtils::getString(item, "txt4");
    metadata.source_country = singer_class.country;
    metadata.avatar_url = normalizeMiguResourceUrl(JsonUtils::getString(item, "img"));
    metadata.source_index_tab = std::string(tab);
    metadata.source_index_order = index_order;
    return metadata;
}

void collectSingerSources(
    const nlohmann::json &node,
    const domain::CountryGender &singer_class,
    std::string_view tab,
    std::uint32_t &next_index_order,
    std::vector<application::model::SingerSourceMetadata> &singers
)
{
    if (node.is_array()) {
        for (const auto &item : node) {
            collectSingerSources(item, singer_class, tab, next_index_order, singers);
        }
        return;
    }

    if (!node.is_object()) {
        return;
    }

    if (auto metadata = parseSingerSourceFromItem(node, singer_class, tab, next_index_order)) {
        ++next_index_order;
        singers.push_back(std::move(*metadata));
        return;
    }

    if (const auto *data = JsonUtils::getObject(node, "data")) {
        collectSingerSources(*data, singer_class, tab, next_index_order, singers);
    }
    if (const auto *contents = JsonUtils::getArray(node, "contents")) {
        collectSingerSources(*contents, singer_class, tab, next_index_order, singers);
    }
}

std::vector<application::model::SingerSourceMetadata> parseSingerSourcesRaw(const std::vector<char> &raw_data, std::string_view tab)
{
    std::vector<application::model::SingerSourceMetadata> singers;
    auto singer_class = singerClassFromTab(tab);
    if (!singer_class) {
        return singers;
    }

    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return singers;
    }

    std::uint32_t next_index_order = 1;
    collectSingerSources(*root_opt, *singer_class, tab, next_index_order, singers);
    return singers;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
