#include "crawler/infrastructure/music/migu/parser/MiguMetadataBatchParser.h"
#include "MiguParserSupportInternal.h"
#include "crawler/infrastructure/json/JsonUtils.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

application::model::MetadataWriteBatch MiguMetadataBatchParser::parseMetadataBatch(const std::vector<char> &body) const
{
    application::model::MetadataWriteBatch batch;
    const auto root = json::JsonUtils::parse(body);
    if (!root) return batch;
    detail::collectSongSources(*root, batch.songs);
    detail::collectAlbumSources(*root, batch.albums);
    std::uint32_t order = 1;
    detail::collectSingerSources(*root, domain::CountryGender {}, {}, order, batch.singers);
    auto profile = detail::parseSingerSourceProfileObject(*root, false);
    if (!profile.source_singer_id.empty() && (profile.metadata || profile.source_song_total || profile.source_album_total)) {
        if (profile.metadata) batch.singers.push_back(*profile.metadata);
        batch.profiles.push_back(std::move(profile));
    }
    return batch;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
