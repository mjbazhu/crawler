#pragma once

#include <cstddef>

namespace orchestrator::modules::crawler::infrastructure::music::migu {

inline constexpr std::size_t kSingerSongPageSize = 50;  // 当前歌手歌曲接口默认每页容量
inline constexpr std::size_t kSingerAlbumPageSize = 10; // 当前歌手专辑接口默认每页容量

} // namespace orchestrator::modules::crawler::infrastructure::music::migu
