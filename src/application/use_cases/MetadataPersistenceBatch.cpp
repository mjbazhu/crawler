#include "crawler/application/use_cases/MetadataPersistenceService.h"

#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

// 封面文件与数据库组成补偿事务；每个文件为本批次独有，不覆盖历史文件。
class PendingMetadataCovers {
public:
    // 保存可选封面 port；没有地址的来源不会调用该 port。
    explicit PendingMetadataCovers(std::shared_ptr<application::ports::MetadataCoverStore> store) : store_(std::move(store))
    {}

    // 数据库未提交时尽力清理本次已完整保存的封面。
    ~PendingMetadataCovers()
    {
        if (committed_ || !store_) return;
        for (const auto &path : paths_) {
            try {
                (void)store_->discard(path);
            } catch (...) { /* 清理不能覆盖原始持久化错误。 */
            }
        }
    }

    // 把一次下载结果归入实体，失败也作为资源状态提交，远端地址不入库。
    application::model::MetadataResourceFile save(const std::string &source, std::string_view kind, const std::string &id, const std::string &url)
    {
        application::model::MetadataResourceFile result;
        if (!store_) {
            result.status = 3;
            result.error_category = "persistence";
            result.result_code = "missing_cover_store";
            result.last_error = "metadata cover store is not configured";
            return result;
        }
        result = store_->save(source, kind, id, url);
        if (result.status == 2 && !result.local_path.empty()) paths_.push_back(result.local_path);
        return result;
    }

    // 数据库提交后，文件由对应资源记录持有。
    void commit()
    {
        committed_ = true;
    }

private:
    std::shared_ptr<application::ports::MetadataCoverStore> store_; // 封面保存与回滚 port
    std::vector<std::string> paths_;                                // 本次新建文件
    bool committed_ = false;                                        // 数据库是否已提交
};

} // namespace

bool MetadataPersistenceService::saveMetadataBatch(application::model::MetadataWriteBatch batch)
{
    batch = buildMetadataWriteBatch(std::move(batch));
    auto dependencies = ingestion_;
    return persistence_runner_.run<bool>([dependencies, batch = std::move(batch)]() mutable {
        PendingMetadataCovers covers(dependencies.covers);
        for (auto &singer : batch.singers) {
            if (!singer.avatar_url.empty() && !singer.cover_resource) {
                singer.cover_resource = covers.save(singer.source, "singers", singer.source_singer_id, singer.avatar_url);
            }
        }
        for (auto &album : batch.albums) {
            if (!album.cover_url.empty() && !album.cover_resource) {
                album.cover_resource = covers.save(album.source, "albums", album.source_album_id, album.cover_url);
            }
        }
        if (!dependencies.writer) return false;
        auto outcome = dependencies.writer->saveBatch(batch);
        const bool saved = outcome.isSuccess();
        if (!saved && dependencies.diagnostics) dependencies.diagnostics->error("MetadataPersistenceService", outcome.message());
        if (saved) {
            covers.commit();
            if (dependencies.covers && outcome.data()) {
                for (const auto &path : outcome.data()->replaced_cover_paths) {
                    bool removed = false;
                    try {
                        removed = dependencies.covers->discard(path);
                    } catch (...) { /* 提交后的清理不能把已提交事务报告成失败。 */
                    }
                    if (!removed && dependencies.diagnostics) dependencies.diagnostics->warn("MetadataPersistenceService", "replaced cover cleanup failed");
                }
            }
        }
        return saved;
    },
                                         false,
                                         "save metadata batch");
}

bool MetadataPersistenceService::saveAlbumDetails(const std::vector<application::model::AlbumSourceMetadata> &albums, const std::vector<application::model::SongSourceMetadata> &songs)
{
    if (!ingestion_.writer) {
        return (albums.empty() || saveAlbumSources(albums, static_cast<int>(albums.size()))) && (songs.empty() || saveSongSources(songs, static_cast<int>(songs.size())));
    }
    application::model::MetadataWriteBatch batch;
    batch.albums = albums;
    batch.songs = songs;
    return (!batch.albums.empty() || !batch.songs.empty()) && saveMetadataBatch(std::move(batch));
}

bool MetadataPersistenceService::hasAtomicWriter() const
{
    return static_cast<bool>(ingestion_.writer);
}

bool MetadataPersistenceService::saveSources(application::model::MetadataWriteBatch batch, int limit)
{
    if (limit > 0) {
        const auto count = static_cast<std::size_t>(limit);
        if (batch.singers.size() > count) batch.singers.resize(count);
        if (batch.albums.size() > count) batch.albums.resize(count);
        if (batch.songs.size() > count) batch.songs.resize(count);
    }
    if (!ingestion_.writer || (batch.singers.empty() && batch.albums.empty() && batch.songs.empty())) return false;
    return saveMetadataBatch(std::move(batch));
}

} // namespace orchestrator::modules::crawler::application::use_cases
