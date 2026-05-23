#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"

#include "crawler/infrastructure/json/JsonUtils.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

namespace {

using json::JsonUtils;

// 返回 listen 响应中的 data 对象；缺失时返回空指针。
const nlohmann::json *songResourceDataObject(const nlohmann::json &root)
{
    return JsonUtils::getObject(root, "data");
}

} // namespace

application::model::SongResourceFetchResult MiguSongResourceParser::parseSongResource(const std::vector<char> &raw_data) const
{
    application::model::SongResourceFetchResult result;
    auto root_opt = JsonUtils::parse(raw_data);
    if (!root_opt || !root_opt->is_object()) {
        return result;
    }

    const auto &root = *root_opt;
    result.code = JsonUtils::getString(root, "code");
    result.message = JsonUtils::getString(root, "info");

    const auto *data = songResourceDataObject(root);
    if (!data) {
        return result;
    }

    result.url = JsonUtils::getString(*data, "url");
    result.version = JsonUtils::getString(*data, "version");
    result.audio_format_type = JsonUtils::getString(*data, "audioFormatType");
    result.lyric_url = JsonUtils::getString(*data, "lrcUrl");
    result.mrc_url = JsonUtils::getString(*data, "mrcUrl");
    result.trc_url = JsonUtils::getString(*data, "trcUrl");
    if (result.url.empty()) {
        const std::string cannot_code = JsonUtils::getString(*data, "cannotCode");
        const auto *dialog = JsonUtils::getObject(*data, "dialogInfo");
        const std::string dialog_text = dialog ? JsonUtils::getString(*dialog, "text") : std::string {};
        if (!cannot_code.empty()) {
            result.code = cannot_code;
        }
        if (!dialog_text.empty()) {
            result.message = dialog_text;
        }
    }
    result.ok = result.code == "000000" && !result.url.empty();
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
