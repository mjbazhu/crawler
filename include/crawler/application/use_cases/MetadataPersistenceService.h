#pragma once

#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/AlbumRepository.h"
#include "crawler/application/ports/persistence/AlbumSourceWriter.h"
#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/application/ports/storage/FileStore.h"
#include "crawler/application/model/SingerSourceProfile.h"
#include "crawler/application/model/MetadataWriteBatch.h"
#include "crawler/application/use_cases/TaskRunner.h"
#include "crawler/application/use_cases/MetadataIngestionDependencies.h"
#include "crawler/domain/music/model/ParseResult.h"

#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 元数据持久化服务，统一封装文件保存和各类 repository 写入分发。
class MetadataPersistenceService {
public:
    // 保存所有持久化 port，写入动作会通过 persistence_executor 执行。
    MetadataPersistenceService(
        std::shared_ptr<application::ports::FileStore> file_store,
        std::shared_ptr<application::ports::SongRepository> song_repository,
        std::shared_ptr<application::ports::AlbumRepository> album_repository,
        std::shared_ptr<application::ports::SingerRepository> singer_repository,
        std::shared_ptr<application::ports::SongSourceRepository> song_source_repository,
        std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository,
        std::shared_ptr<application::ports::AlbumSourceWriter> album_source_writer,
        std::shared_ptr<application::ports::TaskExecutor> persistence_executor,
        MetadataIngestionDependencies ingestion = {}
    );

    // 保存通用解析结果，自动分发到文件存储和领域仓储。
    bool saveParseResults(const std::vector<domain::ParseResult> &results, int limit);

    // 保存平台歌曲来源元数据。
    bool saveSongSources(const std::vector<application::model::SongSourceMetadata> &songs, int limit);

    // 保存平台歌手来源元数据。
    bool saveSingerSources(const std::vector<application::model::SingerSourceMetadata> &singers, int limit);

    // 保存平台歌手来源概览总数。
    bool saveSingerSourceProfile(const application::model::SingerSourceProfile &profile);

    // 保存平台专辑来源元数据。
    bool saveAlbumSources(const std::vector<application::model::AlbumSourceMetadata> &albums, int limit);

    // 把同一份详情中的专辑、歌曲、歌手关系和资源作为一次写入提交。
    bool saveAlbumDetails(const std::vector<application::model::AlbumSourceMetadata> &albums, const std::vector<application::model::SongSourceMetadata> &songs);

    // 保留详情响应的全部实体和同步结果，避免丢弃独立歌手片段。
    bool saveAlbumDetails(application::model::MetadataWriteBatch batch);

    // 同一响应中的独立实体一次提交，数量限制在拆分关系之前应用。
    bool saveSources(application::model::MetadataWriteBatch batch, int limit);

    // 返回是否已装配原子写入能力，供 legacy 入口避免重复公共表写入。
    bool hasAtomicWriter() const;

    // 保存领域歌曲。
    bool saveSongs(const std::vector<domain::Song> &songs, int limit);

    // 保存领域专辑，并兼容保存专辑内已有歌曲。
    bool saveAlbums(const std::vector<domain::Album> &albums, int limit);

    // 保存领域歌手。
    bool saveSingers(const std::vector<domain::Singer> &singers);

private:
    // 封面在事务外获取；数据库失败时回收本次新建文件。
    bool saveMetadataBatch(application::model::MetadataWriteBatch batch);

    MetadataIngestionDependencies ingestion_;                                              // 元数据事务和封面依赖
    std::shared_ptr<application::ports::FileStore> file_store_;                            // 文件存储 port
    std::shared_ptr<application::ports::SongRepository> song_repository_;                  // 歌曲仓储 port
    std::shared_ptr<application::ports::AlbumRepository> album_repository_;                // 专辑仓储 port
    std::shared_ptr<application::ports::SingerRepository> singer_repository_;              // 歌手仓储 port
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository_;     // 歌曲来源仓储 port
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository_; // 歌手来源仓储 port
    std::shared_ptr<application::ports::AlbumSourceWriter> album_source_writer_;           // 专辑来源写入 port
    TaskRunner persistence_runner_;                                                        // 持久化执行器
};

} // namespace orchestrator::modules::crawler::application::use_cases
