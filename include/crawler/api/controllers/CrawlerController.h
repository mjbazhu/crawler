#pragma once

#include "crawler/api/dto/CrawlerSettingsDto.h"
#include "crawler/api/http/HttpModels.h"
#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/application/use_cases/MiguPacmtokenService.h"
#include "crawler/application/use_cases/MetadataCoverService.h"
#include "crawler/application/use_cases/LegacyResourceTransferService.h"

#include <memory>

namespace orchestrator::modules::crawler::api {
namespace controllers {

// 爬虫 HTTP 控制器，负责把 API 请求转换为 application 用例调用。
class CrawlerController {
public:
    // 构造函数：注入爬虫用例服务。
    explicit CrawlerController(
        std::shared_ptr<application::use_cases::CrawlerService> service,
        dto::CrawlerSettingsReadonlySnapshot readonly_settings = {},
        std::shared_ptr<application::use_cases::MiguPacmtokenService> pacmtoken_service = nullptr,
        std::shared_ptr<application::use_cases::MetadataCoverService> cover_service = nullptr
    );
    // 组合根启用并行兼容后注入，旧构造与旧部署保持不变。
    void setLegacyResourceTransfers(std::shared_ptr<application::use_cases::LegacyResourceTransferService> service)
    {
        legacy_transfers_ = std::move(service);
    }

    // 处理 Migu 歌手分类采集请求，创建后台任务后立即返回任务 ID。
    http::HttpResponse handleStartMiguSingerIndexCrawl(http::HttpRequest req);

    // 从库选择来源并独立刷新整份歌手资料，不抓取目录或下载图片。
    http::HttpResponse handleRefreshMiguSingerProfilesFromDb(http::HttpRequest req);

    // 处理 Migu 从库认领歌手并采集歌曲请求，返回批量任务 ID。
    http::HttpResponse handleStartMiguSingerSongsCrawlFromDb(http::HttpRequest req);

    // 预览 Migu 从库歌手歌曲采集命中范围，不认领目标。
    http::HttpResponse handlePreviewMiguSingerSongsCrawlFromDb(http::HttpRequest req);


    // 处理 Migu 从库认领歌手并采集专辑请求，返回批量任务 ID。
    http::HttpResponse handleStartMiguSingerAlbumsCrawlFromDb(http::HttpRequest req);

    // 预览 Migu 从库歌手专辑采集命中范围，不认领目标。
    http::HttpResponse handlePreviewMiguSingerAlbumsCrawlFromDb(http::HttpRequest req);




    // 处理 Migu 任务停止请求。
    http::HttpResponse handleStopMiguBatch(http::HttpRequest req);

    // 查询 Migu 批量采集任务进度。
    http::HttpResponse handleGetMiguBatchProgress(http::HttpRequest req);

    // 查询持久化批次列表。
    http::HttpResponse handleListMiguTasks(http::HttpRequest req);

    // 查询持久化批次详情。
    http::HttpResponse handleGetMiguTask(http::HttpRequest req);

    // 查询持久化批次目标列表。
    http::HttpResponse handleListMiguTaskItems(http::HttpRequest req);

    // 查询持久化批次事件列表。
    http::HttpResponse handleListMiguTaskEvents(http::HttpRequest req);

    // 查询批次全局指标。
    http::HttpResponse handleGetMiguTaskSummary(http::HttpRequest req);

    // 从批次失败目标创建新批次。
    http::HttpResponse handleRerunFailedMiguTask(http::HttpRequest req);

    // 克隆批次目标创建新批次。
    http::HttpResponse handleCloneMiguTask(http::HttpRequest req);

    // 从批次 item 创建补池重试。
    http::HttpResponse handleBackfillRetryMiguTask(http::HttpRequest req);

    // 归档批次。
    http::HttpResponse handleArchiveMiguTask(http::HttpRequest req);

    // 查询 Migu 采集控制台总览。
    http::HttpResponse handleGetMiguOverview(http::HttpRequest req);

    // 查询 Migu 歌手列表。
    http::HttpResponse handleListMiguSingers(http::HttpRequest req);

    // 查询 Migu 歌手详情。
    http::HttpResponse handleGetMiguSinger(http::HttpRequest req);

    // 查询 Migu 歌手歌曲。
    http::HttpResponse handleListMiguSingerSongs(http::HttpRequest req);

    // 查询 Migu 歌手专辑。
    http::HttpResponse handleListMiguSingerAlbums(http::HttpRequest req);

    // 查询 Migu 专辑列表。
    http::HttpResponse handleListMiguAlbums(http::HttpRequest req);

    // 查询 Migu 专辑详情。
    http::HttpResponse handleGetMiguAlbum(http::HttpRequest req);

    // 查询 Migu 专辑歌曲。
    http::HttpResponse handleListMiguAlbumSongs(http::HttpRequest req);

    // 查询 Migu 歌曲列表。
    http::HttpResponse handleListMiguSongs(http::HttpRequest req);

    // 查询 Migu 歌曲详情。
    http::HttpResponse handleGetMiguSong(http::HttpRequest req);

    // 查询 Migu 资源队列。
    http::HttpResponse handleListMiguResources(http::HttpRequest req);

    // 预览 Migu 资源治理影响范围。
    http::HttpResponse handlePreviewMiguResources(http::HttpRequest req);

    // 重试 Migu 资源队列目标。
    http::HttpResponse handleRetryMiguResources(http::HttpRequest req);

    // 重置 Migu 资源队列目标。
    http::HttpResponse handleResetMiguResources(http::HttpRequest req);

    // 恢复 Migu 资源队列超时获取中的目标。
    http::HttpResponse handleRecoverStaleMiguResources(http::HttpRequest req);

    // 处理 Migu 资源队列，执行 URL 获取、下载、保存和回写。
    http::HttpResponse handleProcessMiguResources(http::HttpRequest req);

    // 查询歌手/专辑封面队列。
    http::HttpResponse handleListMiguCovers(http::HttpRequest req);
    // 下载已入库 URL 对应的封面，默认后台执行。
    http::HttpResponse handleProcessMiguCovers(http::HttpRequest req);
    // 恢复封面超时认领。
    http::HttpResponse handleRecoverMiguCovers(http::HttpRequest req);
    // 读取指定所有者的已提交图片，支持 ETag 条件请求。
    http::HttpResponse handleGetMiguCoverContent(http::HttpRequest req);

    // 查询 Migu 补池队列。
    http::HttpResponse handleListMiguBackfillQueue(http::HttpRequest req);

    // 预览 Migu 补池筛选命中和可操作范围。
    http::HttpResponse handlePreviewMiguBackfill(http::HttpRequest req);

    // 重试 Migu 补池目标。
    http::HttpResponse handleRetryMiguBackfill(http::HttpRequest req);

    // 重置 Migu 补池目标。
    http::HttpResponse handleResetMiguBackfill(http::HttpRequest req);

    // 查询 Migu 元数据一致性摘要。
    http::HttpResponse handleGetMiguIntegrity(http::HttpRequest req);

    // 查询 MySQL schema 版本和关键对象完整性。
    http::HttpResponse handleGetMiguSchemaStatus(http::HttpRequest req);

    // 恢复 Migu 超时停留在同步中的元数据和批次状态。
    http::HttpResponse handleRecoverMiguStale(http::HttpRequest req);

    // 查询治理审计事件。
    http::HttpResponse handleListMiguGovernanceEvents(http::HttpRequest req);

    // 返回前端控制台需要的固定字典和枚举。
    http::HttpResponse handleGetMiguDictionaries(http::HttpRequest req);

    // 查询 Migu 运行设置。
    http::HttpResponse handleGetMiguSettings(http::HttpRequest req);

    // 更新 Migu 运行设置中支持热变更的字段。
    http::HttpResponse handleUpdateMiguSettings(http::HttpRequest req);

    // 刷新并更新运行期 Migu pacmtoken。
    http::HttpResponse handleRefreshMiguPacmtoken(http::HttpRequest req);

    // 查询运行期 pacmtoken 脱敏状态。
    http::HttpResponse handleGetMiguPacmtokenState(http::HttpRequest req);

    // 返回当前运行期 pacmtoken 明文，供前端显式同步本地状态。
    http::HttpResponse handleGetCurrentMiguPacmtoken(http::HttpRequest req);

    // 返回 OpenAPI JSON 文档，供前端或调试工具接入。
    http::HttpResponse handleOpenApi(http::HttpRequest req);

private:
    std::shared_ptr<application::use_cases::LegacyResourceTransferService> legacy_transfers_; // 持久旧视图
    std::shared_ptr<application::use_cases::MetadataCoverService> cover_service_;     // 独立封面用例
    std::shared_ptr<application::use_cases::CrawlerService> service_;                 // application 用例服务
    dto::CrawlerSettingsReadonlySnapshot readonly_settings_;                          // settings API 只读启动期快照
    std::shared_ptr<application::use_cases::MiguPacmtokenService> pacmtoken_service_; // pacmtoken 刷新用例
};

}
} // namespace orchestrator::modules::crawler::api::controllers
