#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"

#include <charconv>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 已返回曲目少于平台声明总数时保持部分完成，不能把截断或受限响应标记为齐全。
inline int albumSongSyncStatus(const std::vector<application::model::AlbumSourceMetadata> &details, int received)
{
    for (const auto &album : details) {
        int expected = -1;
        const auto &text = album.total_count;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), expected);
        if (parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() && expected >= 0 && received < expected) return 4;
    }
    return 2;
}

} // namespace orchestrator::modules::crawler::application::use_cases
