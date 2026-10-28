#include "crawler/application/use_cases/SearchCollectionWorkflow.h"
#include "crawler/application/use_cases/WorkflowPaging.h"
#include "crawler/application/model/AlbumCollectionPolicy.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {
// 单页只负责取数和解析，写入留给顺序归并阶段。
struct SourceSearchPage {
    int page = 0;                                 // 稳定分页顺序
    application::model::MetadataWriteBatch batch; // 当前响应的实体上下文
    bool fetched = false;                         // 网络响应是否可用
};
} // namespace

bool SearchCollectionWorkflow::searchSourcesAndSave(const std::string &keyword, int limit, int max_page_concurrency)
{
    bool saved_any = false;
    const int last_page = (limit + kWorkflowSearchRows - 1) / kWorkflowSearchRows;
    const int window = workflowPageWindow(max_page_concurrency);
    for (const bool songs : {true, false}) {
        int processed = 0;
        for (int first = 1; first <= last_page && processed < limit; first += window) {
            auto pages = collectOrderedPageWindow<SourceSearchPage>(page_runner_, first, (std::min)(last_page, first + window - 1), [this, &keyword, songs](int page) {
                    SourceSearchPage result;
                    result.page = page;
                    auto response = songs ? song_search_provider_->fetchSongSearchPage(keyword, page, kWorkflowSearchRows)
                                          : album_search_provider_->fetchAlbumSearchPage(keyword, page, kWorkflowSearchRows);
                    if (!response || response->status_code < 200 || response->status_code >= 300) return result;
                    result.fetched = true;
                    result.batch = parser_->parseSources(std::move(response->body));
                    return result; }, "collect search sources");
            for (auto &page : pages) {
                if (processed >= limit) break;
                if (!page.fetched) return false;
                const auto remaining = static_cast<std::size_t>(limit - processed);
                auto &batch = page.batch;
                const auto available = songs ? batch.songs.size() : batch.albums.size();
                if (available == 0) break;
                if (songs && batch.songs.size() > remaining) batch.songs.resize(remaining);
                if (!songs && batch.albums.size() > remaining) batch.albums.resize(remaining);
                // 只展开已选中的专辑；不为歌曲逐首请求歌手或专辑详情。
                auto selected_albums = batch.albums;
                if (!persistence_->saveSources(std::move(batch), 0)) return false;
                saved_any = true;
                processed += static_cast<int>((std::min)(available, remaining));
                if (!songs && album_detail_provider_) {
                    for (const auto &album : selected_albums) {
                        if (application::model::isSharedReleaseArchive(album)) continue;
                        auto detail = album_detail_provider_->fetchAlbumDetail(album.source_album_id, album.source_res_type);
                        if (!detail || detail->status_code < 200 || detail->status_code >= 300) return false;
                        auto details = parser_->parseSources(std::move(detail->body));
                        const bool matched = std::any_of(details.albums.begin(), details.albums.end(), [&](const auto &item) {
                            return item.source == album.source && item.detail_sync_status == 2 && (item.source_album_id == album.source_album_id || item.source_material_id == album.source_album_id);
                        });
                        if (!matched || !persistence_->saveSources(std::move(details), 0)) return false;
                    }
                }
            }
        }
    }
    return saved_any;
}
} // namespace orchestrator::modules::crawler::application::use_cases
