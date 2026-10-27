#include "crawler/infrastructure/music/migu/lyrics/MiguMrcLyricsDecoder.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {

MiguMrcLyricsDecoder::MiguMrcLyricsDecoder(std::string_view key) : cipher_(key)
{}

application::result::ServiceResult<domain::TimedLyrics> MiguMrcLyricsDecoder::decode(std::string_view content) const
{
    const auto plain = cipher_.decrypt(content);
    if (!plain.isSuccess()) return application::result::ServiceResult<domain::TimedLyrics>::failure(plain.error(), plain.message());
    return parser_.parse(*plain.data());
}

std::string_view MiguMrcLyricsDecoder::version() const
{
    return "migu-mrc64-v1";
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
