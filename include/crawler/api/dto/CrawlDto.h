#pragma once

#include "crawler/api/http/HttpModels.h"
#include "crawler/application/model/CrawlerRuntimeSettings.h"
#include "crawler/application/model/MiguPacmtoken.h"
#include "crawler/application/model/SingerSourceSync.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::api {
namespace dto {

// Migu 歌手分类采集请求 DTO，支持单 tab 或 tab 数组。
struct MiguSingerIndexCrawlRequestDto {
    std::vector<std::string> tabs; // 歌手分类 tab 列表
    int limit = 20;                // 每个 tab 最大采集数量，0 表示当前分类全部
    int start_index = 0;           // 选中区间起点，闭区间
    std::optional<int> end_index;  // 选中区间终点，开区间

    // 从原始 HTTP 请求构造 DTO。
    static std::optional<MiguSingerIndexCrawlRequestDto> fromRequest(const http::HttpRequest &req);

    // 校验 tabs、limit 和 [start_index, end_index) 范围。
    std::optional<std::string> validate() const;

    // 返回当前请求选中的 Migu 目标。
    std::vector<std::string> selectedTargets() const;
};

// Migu 歌手 ID 采集请求 DTO，用于歌手歌曲和歌手专辑列表。
struct MiguSingerIdsCrawlRequestDto {
    std::vector<std::uint64_t> singer_ids; // Migu 歌手 ID 列表
    int limit = 0;                         // 每个歌手最大采集数量，0 表示全部
    int start_index = 0;                   // 选中区间起点，闭区间
    std::optional<int> end_index;          // 选中区间终点，开区间

    // 从原始 HTTP 请求构造 DTO。
    static std::optional<MiguSingerIdsCrawlRequestDto> fromRequest(const http::HttpRequest &req);

    // 校验 singer_ids、limit 和 [start_index, end_index) 范围。
    std::optional<std::string> validate() const;

    // 返回当前请求选中的歌曲采集目标。
    std::vector<std::string> selectedSongTargets() const;

    // 返回当前请求选中的专辑采集目标。
    std::vector<std::string> selectedAlbumTargets() const;
};

// Migu 专辑补齐请求 DTO，用于详情补齐和专辑歌曲补齐。
struct MiguAlbumBackfillRequestDto {
    std::string source = "migu"; // 来源标识，目前仅允许 migu
    int limit = 20;              // 最大补齐专辑数量

    // 从原始 HTTP 请求构造 DTO。
    static std::optional<MiguAlbumBackfillRequestDto> fromRequest(const http::HttpRequest &req);

    // 校验 source 和 limit。
    std::optional<std::string> validate() const;
};

// Migu 从库歌手来源采集请求 DTO，用于按 singer_source_refs 状态批量获取歌曲或专辑。
struct MiguSingerFromDbCrawlRequestDto {
    application::model::SingerSourceSyncQuery query; // 歌手来源筛选和认领参数
    int limit_per_singer = 0;                        // 每个歌手最大采集数量，0 表示全量

    // 从原始 HTTP 请求构造 DTO。
    static std::optional<MiguSingerFromDbCrawlRequestDto> fromRequest(const http::HttpRequest &req);

    // 校验 source、状态、筛选条件和 limit。
    std::optional<std::string> validate() const;
};

// 批量任务控制 DTO，用于取消等任务级操作。
struct BatchControlRequestDto {
    std::string batch_id; // 任务 ID

    // 从 body 或 query 解析任务 ID。
    static std::optional<BatchControlRequestDto> fromRequest(const http::HttpRequest &req);

    // 校验任务 ID。
    std::optional<std::string> validate() const;
};

// Migu 运行设置更新 DTO，只允许更新可热变更字段。
struct MiguSettingsUpdateDto {
    application::model::CrawlerRuntimeSettingsUpdate update; // 可热更新运行设置

    // 从原始 HTTP 请求解析设置更新。
    static std::optional<MiguSettingsUpdateDto> fromRequest(const http::HttpRequest &req);

    // 校验至少提供一个可更新字段。
    std::optional<std::string> validate() const;
};

// Migu pacmtoken 刷新 DTO，用于前端运行期传入并刷新 token。
struct MiguPacmtokenRefreshDto {
    application::model::MiguPacmtokenRefreshRequest request; // application 刷新请求

    // 从原始 HTTP 请求解析 pacmtoken 或包含 pacmtoken 的 Cookie。
    static std::optional<MiguPacmtokenRefreshDto> fromRequest(const http::HttpRequest &req);

    // 校验 token 长度和 header 安全边界；空 token 允许由后端运行期存储兜底。
    std::optional<std::string> validate() const;
};

}
} // namespace orchestrator::modules::crawler::api::dto
