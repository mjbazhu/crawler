#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

namespace {

// 歌手概览 tab 解析状态，用于区分“平台明确没有该 tab”和“响应结构无法确认”。
struct SingerProfileParseState {
    bool saw_tab_scroll = false; // 是否看到歌手详情的 tab 容器
    bool saw_song_tab = false;   // 是否看到歌曲 tab
    bool saw_album_tab = false;  // 是否看到专辑 tab
};

// 读取非负整数字段；字段不存在或格式不合法时保持未知。
std::optional<int> optionalNonNegativeInt(const nlohmann::json &item, const std::string &key)
{
    if (!item.is_object() || !item.contains(key)) {
        return std::nullopt;
    }
    const auto value = JsonUtils::getInt(item, key, -1);
    if (value < 0 || value > static_cast<std::int64_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

// 从歌手详情项里尽量提取平台歌手 ID。
void collectSingerProfileIdentity(const nlohmann::json &item, application::model::SingerSourceProfile &profile)
{
    if (JsonUtils::getString(item, "view") != "ZJ-SingerDetail-Item") {
        return;
    }

    profile.source_singer_id = firstNonEmpty({
        JsonUtils::getString(item, "resId"),
        extractActionId(JsonUtils::getString(item, "action")),
        extractViewIdSuffix(JsonUtils::getString(item, "viewId")),
    });
    const auto name = JsonUtils::getString(item, "txt");
    if (!profile.source_singer_id.empty() && !name.empty()) {
        application::model::SingerSourceMetadata singer;
        singer.source = profile.source;
        singer.source_singer_id = profile.source_singer_id;
        singer.name = name;
        singer.normalized_name = name;
        singer.source_name_pinyin = firstNonEmpty({JsonUtils::getString(item, "nameSpelling"), JsonUtils::getString(item, "namePinyin")});
        singer.avatar_url = normalizeMiguSingerAvatarUrl(JsonUtils::getString(item, "img"));
        profile.metadata = std::move(singer);
    }
}

// 从 tab 项里提取歌曲和专辑平台声明总数。
void collectSingerProfileTabCount(const nlohmann::json &item, application::model::SingerSourceProfile &profile, SingerProfileParseState &state)
{
    if (JsonUtils::getString(item, "view") != "ZJ-Tab-Item") {
        return;
    }

    const std::string action = JsonUtils::getString(item, "action");
    if (action == "song") {
        state.saw_song_tab = true;
        if (auto total = optionalNonNegativeInt(item, "txt2")) {
            profile.source_song_total = *total;
        }
    } else if (action == "album") {
        state.saw_album_tab = true;
        if (auto total = optionalNonNegativeInt(item, "txt2")) {
            profile.source_album_total = *total;
        }
    }
}

// 递归遍历 Migu 响应树，避免响应内容块层级调整后解析失效。
void collectSingerProfile(const nlohmann::json &node, application::model::SingerSourceProfile &profile, SingerProfileParseState &state)
{
    if (node.is_array()) {
        for (const auto &item : node) {
            collectSingerProfile(item, profile, state);
        }
        return;
    }

    if (!node.is_object()) {
        return;
    }

    if (JsonUtils::getString(node, "view") == "ZJ-Tab-Scroll") {
        state.saw_tab_scroll = true;
    }
    collectSingerProfileIdentity(node, profile);
    collectSingerProfileTabCount(node, profile, state);

    if (const auto *data = JsonUtils::getObject(node, "data")) {
        collectSingerProfile(*data, profile, state);
    }
    if (const auto *contents = JsonUtils::getArray(node, "contents")) {
        collectSingerProfile(*contents, profile, state);
    }
}

} // namespace

application::model::SingerSourceProfile parseSingerSourceProfileRaw(const std::vector<char> &raw_data)
{
    application::model::SingerSourceProfile profile;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return profile;
    }

    return parseSingerSourceProfileObject(*root_opt, true);
}

application::model::SingerSourceProfile parseSingerSourceProfileObject(const nlohmann::json &root, bool complete_profile)
{
    application::model::SingerSourceProfile profile;
    // 错误业务码即使携带可解析的资料，也不能成为成功刷新依据。
    if (root.contains("code") && JsonUtils::getString(root, "code") != "000000") return profile;
    SingerProfileParseState state;
    collectSingerProfile(root, profile, state);
    if ((complete_profile || profile.metadata) && state.saw_tab_scroll) {
        if (!state.saw_song_tab && !profile.source_song_total) {
            profile.source_song_total = 0;
        }
        if (!state.saw_album_tab && !profile.source_album_total) {
            profile.source_album_total = 0;
        }
    }
    return profile;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
