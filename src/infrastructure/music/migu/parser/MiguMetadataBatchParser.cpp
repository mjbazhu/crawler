#include "crawler/infrastructure/music/migu/parser/MiguMetadataBatchParser.h"
#include "MiguParserSupportInternal.h"
#include "crawler/infrastructure/json/JsonUtils.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

application::model::MetadataWriteBatch MiguMetadataBatchParser::parseMetadataBatch(const std::vector<char> &body) const
{
    const auto parsed = parseMetadataResponse(body);
    return parsed.isSuccess() ? *parsed.data() : application::model::MetadataWriteBatch {};
}

application::result::ServiceResult<application::model::MetadataWriteBatch> MiguMetadataBatchParser::parseMetadataResponse(const std::vector<char> &body) const
{
    using Result = application::result::ServiceResult<application::model::MetadataWriteBatch>;
    using Error = application::result::ServiceErrorType;
    application::model::MetadataWriteBatch batch;
    const auto root = json::JsonUtils::parse(body);
    if (!root) return Result::failure(Error::TransientExternalApiError, "metadata JSON parse failed");
    if (root->contains("code") && json::JsonUtils::getString(*root, "code") != "000000") return Result::failure(Error::ExternalApiError, "metadata business response rejected");
    detail::collectSongSources(*root, batch.songs);
    detail::collectAlbumSources(*root, batch.albums);
    std::uint32_t order = 1;
    detail::collectSingerSources(*root, domain::CountryGender {}, {}, order, batch.singers);
    auto profile = detail::parseSingerSourceProfileObject(*root, false);
    if (!profile.source_singer_id.empty() && (profile.metadata || profile.source_song_total || profile.source_album_total)) {
        if (profile.metadata) batch.singers.push_back(*profile.metadata);
        batch.profiles.push_back(std::move(profile));
    }
    return Result::success(std::move(batch));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
