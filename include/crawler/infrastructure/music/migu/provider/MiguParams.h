#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// mg-search-params
struct Params {
    std::optional<std::string> text;
    std::optional<int> pageNo;
    std::optional<int> pageSize;
    std::optional<std::string> searchSwitch;
    std::optional<std::string> needSimple;
    std::optional<uint64_t> resourceId;
    std::optional<uint64_t> albumId; // 专辑曲目分页使用的普通专辑或已核实物料身份
    std::optional<int> resourceType;
    std::optional<uint64_t> singerId;            // 歌手 ID，用于歌手歌曲分页接口
    std::optional<int> type;                     // Migu 歌手歌曲接口类型，当前 type=1 表示歌曲列表
    std::optional<std::string> tab;              // Migu 歌手分类 tab，例如 huayu-nan
    std::optional<std::string> contentId;        // Migu listen 接口内容 ID
    std::optional<std::string> netType;          // Migu listen 接口网络类型
    std::optional<std::string> resourceTypeText; // Migu listen 接口字符串资源类型
    std::optional<std::string> copyrightId;      // Migu listen 接口版权 ID
    std::optional<std::string> toneFlag;         // Migu listen 接口音质档位
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
