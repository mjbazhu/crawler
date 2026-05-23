#pragma once

#include "crawler/application/ports/persistence/AlbumSourceSyncQueue.h"
#include "crawler/application/ports/persistence/AlbumSourceWriter.h"

namespace orchestrator::modules::crawler::application::ports {

// 专辑平台来源仓储聚合 port，用于兼容既有实现和组合根。
class AlbumSourceRepository : public AlbumSourceWriter,
                              public AlbumSourceSyncQueue {
public:
    virtual ~AlbumSourceRepository() = default;
};

} // namespace orchestrator::modules::crawler::application::ports
