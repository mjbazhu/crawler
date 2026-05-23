#pragma once

#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/ports/provider/AlbumSearchProvider.h"
#include "crawler/application/ports/provider/RawPageFetcher.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/provider/SongSearchProvider.h"

namespace orchestrator::modules::crawler::application::ports {

// 音乐平台 provider 聚合 port，用于兼容既有组合根和测试替身。
class IMusicProvider : public SongSearchProvider,
                       public AlbumSearchProvider,
                       public AlbumDetailProvider,
                       public SingerCatalogProvider,
                       public SongResourceProvider,
                       public RawPageFetcher {
public:
    using Response = application::model::ProviderResponse;

    virtual ~IMusicProvider() = default;
};

} // namespace orchestrator::modules::crawler::application::ports
