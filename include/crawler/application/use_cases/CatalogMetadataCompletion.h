#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/MetadataWriteBatch.h"
#include "crawler/application/model/ProviderResponse.h"
#include "crawler/application/ports/persistence/MusicCatalogQueryRepository.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/application/use_cases/ParserDispatch.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 目录采集需要的完整实体响应；不按单个字段建立请求或补齐入口。
struct CatalogMetadataCompletionDependencies {
    std::shared_ptr<application::ports::SingerCatalogProvider> singers;       // 歌手整份资料
    std::shared_ptr<application::ports::AlbumDetailProvider> albums;          // 专辑整份详情
    std::shared_ptr<application::ports::MusicCatalogQueryRepository> catalog; // 已有完整资料查询
    std::shared_ptr<ParserDispatch> parser;                                   // 统一响应分流
};

// 线程安全的目录资料补全；复用数据库和有界响应缓存，不保存文件或决定采集数量。
class CatalogMetadataCompletion {
public:
    // 缓存只在本服务实例内共享，数据库仍是完成状态的依据。
    explicit CatalogMetadataCompletion(CatalogMetadataCompletionDependencies dependencies);

    // 总数未知或显式 refresh 时获取整份歌手资料，随当前目录批次原子写入。
    application::result::ServiceResult<application::model::SingerSourceProfile> singerContext(std::uint64_t singer_id, std::optional<int> songs, std::optional<int> albums, bool refresh = false);

    // 复用目标专辑详情与曲目，缺页按声明总数补齐；关联实体随批次写入，不追踪推荐目录。
    application::result::ServiceResult<application::model::MetadataWriteBatch> completeAlbums(application::model::MetadataWriteBatch batch);

private:
    using Batch = application::model::MetadataWriteBatch;                                 // 一次响应的全部解析结果
    using BatchResult = application::result::ServiceResult<std::shared_ptr<const Batch>>; // 可共享的有界响应

    // 缓存记录同时合并同一实体正在进行的请求。
    struct CachedResponse {
        std::shared_future<BatchResult> result; // 等待发生在互斥锁之外
        std::size_t bytes = 0;                  // 按原始响应体限制缓存大小
    };

    // 合并同一实体的在途请求；refresh 淘汰已完成缓存，失败允许正常重试。
    BatchResult response(const std::string &key, const std::function<std::optional<application::model::ProviderResponse>()> &fetch, bool refresh = false, const application::model::AlbumSourceMetadata *album = nullptr);
    // 曲目缺页与详情共用同一在途请求；部分失败保留数据但不缓存，允许后续重试。
    bool prepareAlbumResponse(Batch &batch, const application::model::AlbumSourceMetadata &album, std::size_t &bytes);
    // 数据库已完整时，仅重用尚在缓存的响应，不为缺失单字段重复获取详情。
    std::optional<BatchResult> cached(const std::string &key);
    // 不匹配实体的响应不能成为后续重试的缓存。
    void forget(const std::string &key, const std::shared_ptr<const Batch> &invalid);

    CatalogMetadataCompletionDependencies dependencies_; // 窄端口依赖
    std::mutex mutex_;                                   // 仅保护缓存，不覆盖网络、解析或等待
    std::map<std::string, CachedResponse> responses_;    // 进行中及最近成功的完整响应
    std::size_t cached_bytes_ = 0;                       // 缓存原始字节总量
};

} // namespace orchestrator::modules::crawler::application::use_cases
