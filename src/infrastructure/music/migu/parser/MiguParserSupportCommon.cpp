#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"

#include <algorithm>
#include <cstdint>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

namespace {

constexpr std::string_view kMiguResourcePrefix = "https://d.musicapp.migu.cn";

} // namespace

std::string firstNonEmpty(std::initializer_list<std::string> values)
{
    for (const auto &value : values) {
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

int parseTriStateFlag(const nlohmann::json &item, std::string_view key)
{
    if (!item.is_object()) {
        return -1;
    }

    const auto iter = item.find(std::string(key));
    if (iter == item.end() || iter->is_null()) {
        return -1;
    }

    if (iter->is_boolean()) {
        return iter->get<bool>() ? 1 : 0;
    }
    if (iter->is_number_integer() || iter->is_number_unsigned()) {
        return iter->get<std::int64_t>() != 0 ? 1 : 0;
    }
    if (iter->is_string()) {
        const std::string value = iter->get<std::string>();
        if (value == "1" || value == "true" || value == "TRUE") {
            return 1;
        }
        if (value == "0" || value == "false" || value == "FALSE") {
            return 0;
        }
    }

    return -1;
}

int albumKindFromTypes(std::string_view source_res_type, std::string_view source_resource_type)
{
    if (source_res_type == "1" || source_resource_type == "5") {
        return 2;
    }
    if (source_res_type == "0" || source_resource_type == "2003") {
        return 1;
    }
    return 0;
}

std::string selectImageUrl(const nlohmann::json *items)
{
    if (!items || !items->is_array()) {
        return {};
    }

    for (std::string_view size_type : {"03", "02", "01"}) {
        for (const auto &item : *items) {
            if (JsonUtils::getString(item, "imgSizeType") == size_type) {
                const std::string image = firstNonEmpty({
                    JsonUtils::getString(item, "webpImg"),
                    JsonUtils::getString(item, "img"),
                    JsonUtils::getString(item, "imgOri"),
                });
                if (!image.empty()) {
                    return image;
                }
            }
        }
    }

    for (const auto &item : *items) {
        const std::string image = firstNonEmpty({
            JsonUtils::getString(item, "webpImg"),
            JsonUtils::getString(item, "img"),
            JsonUtils::getString(item, "imgOri"),
        });
        if (!image.empty()) {
            return image;
        }
    }
    return {};
}

std::string normalizeMiguResourceUrl(std::string resource_url)
{
    if (resource_url.empty() || resource_url.starts_with("http://") || resource_url.starts_with("https://")) {
        return resource_url;
    }
    if (resource_url.starts_with("//")) {
        return "https:" + resource_url;
    }
    if (resource_url.starts_with('/')) {
        return std::string(kMiguResourcePrefix) + resource_url;
    }
    if (resource_url.starts_with("data/oss/") || resource_url.starts_with("data/resource-service/")) {
        return std::string(kMiguResourcePrefix) + "/" + resource_url;
    }
    return resource_url;
}

bool hasValidSongItem(const nlohmann::json &item)
{
    const auto *song_item = JsonUtils::getObject(item, "songItem");
    return song_item && JsonUtils::getInt(*song_item, "songId") != 0 && !JsonUtils::getString(*song_item, "songName").empty();
}

bool hasValidAlbumItem(const nlohmann::json &item)
{
    return JsonUtils::getString(item, "view") == "ZJ-Album-Item"
           && !firstNonEmpty({JsonUtils::getString(item, "resId"), extractActionId(JsonUtils::getString(item, "action"))}).empty()
           && !JsonUtils::getString(item, "txt").empty();
}

std::string extractActionId(const std::string &action)
{
    constexpr std::string_view marker = "id=";
    const auto marker_pos = action.find(marker);
    if (marker_pos == std::string::npos) {
        return {};
    }

    std::string id = action.substr(marker_pos + marker.size());
    const auto end_pos = id.find_first_not_of("0123456789");
    if (end_pos != std::string::npos) {
        id = id.substr(0, end_pos);
    }
    return id;
}

std::string extractViewIdSuffix(const std::string &view_id)
{
    const auto pos = view_id.find_last_of('-');
    if (pos == std::string::npos || pos + 1 >= view_id.size()) {
        return {};
    }

    std::string id = view_id.substr(pos + 1);
    const auto end_pos = id.find_first_not_of("0123456789");
    if (end_pos != std::string::npos) {
        id = id.substr(0, end_pos);
    }
    return id;
}

std::optional<domain::CountryGender> singerClassFromTab(std::string_view tab)
{
    const auto it = domain::kTabMap.find(std::string(tab));
    if (it == domain::kTabMap.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t countSingerSongsInContentBlock(const nlohmann::json &content_block)
{
    const auto *items = JsonUtils::getArray(content_block, "contents");
    if (!items) {
        return 0;
    }

    return static_cast<std::size_t>(std::count_if(items->begin(), items->end(), [](const nlohmann::json &item) {
        return hasValidSongItem(item);
    }));
}

std::size_t countSingerAlbums(const nlohmann::json &node)
{
    if (node.is_array()) {
        return static_cast<std::size_t>(std::count_if(node.begin(), node.end(), [](const nlohmann::json &item) {
            return hasValidAlbumItem(item);
        }));
    }
    return 0;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
