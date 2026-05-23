#include "crawler/infrastructure/music/migu/parser/strategy/MiguStrategies.h"

#include "crawler/infrastructure/json/JsonUtils.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy {

using json::JsonUtils;

bool MiguSingerListStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    if (const auto *data = JsonUtils::getObject(root, "data")) {
        if (const auto *contents = JsonUtils::getArray(*data, "contents")) {
            bool matched = false;
            for (const auto &item : *contents) {
                if (JsonUtils::getString(item, "view") != "ZJ-Singer-Item") {
                    continue;
                }

                domain::Singer singer;
                singer.singer_id = static_cast<std::int64_t>(JsonUtils::getInt(item, "resId"));
                singer.name = JsonUtils::getString(item, "txt");
                singer.artwork = JsonUtils::getString(item, "img");
                singer.genre = JsonUtils::getString(item, "txt3");
                singer.gender = domain::Gender::Unknown;
                if (singer.singer_id == 0 || singer.name.empty()) {
                    continue;
                }

                domain::ParseResult res;
                res.suggested_filename = singer.name;
                res.meta_info = std::move(singer);
                results.push_back(std::move(res));
                matched = true;
            }
            return matched;
        }
    }
    return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy
