#pragma once

#include "crawler/domain/music/model/Album.h"
#include "crawler/domain/music/model/Singer.h"
#include "crawler/domain/music/model/Song.h"

#include <string>
#include <variant>
#include <vector>

namespace orchestrator::modules::crawler::domain {

using MetadataVariant = std::variant<Song, Album, Singer>;

struct ParseResult {
    MetadataVariant meta_info;
    std::vector<char> audio_data;
    std::string suggested_filename;
};

} // namespace orchestrator::modules::crawler::domain
