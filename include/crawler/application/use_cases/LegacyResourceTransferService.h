#pragma once

#include "crawler/application/ports/execution/ResourceTransferRuntimeControl.h"
#include "crawler/application/ports/persistence/LegacyResourceTransferRepository.h"
#include "crawler/application/ports/persistence/MetadataCoverProjection.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/ports/storage/MetadataCoverReader.h"

#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// 旧协议只等待其原完成范围，统一worker继续独立处理额外目标。
class LegacyResourceTransferService : public ports::MetadataCoverProjection {
public:
    LegacyResourceTransferService(std::shared_ptr<ports::LegacyResourceTransferRepository> repository, std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime, std::shared_ptr<ports::ResourceTransferAccess> access, std::optional<model::ResourceDestination> legacy_cloud_provider, bool filesystem, std::shared_ptr<ports::MetadataCoverReader> cover_reader = {}, int batch_timeout_ms = 600000);
    // 新请求保持旧筛选与有效options；sync仅在固定范围真实终止后返回。
    result::ServiceResult<model::LegacyResourceTransferBatch> songs(model::SongResourceClaimQuery query, bool sync, int batch_timeout_ms = 0);
    result::ServiceResult<model::LegacyResourceTransferBatch> covers(model::MetadataCoverOptions options, bool sync);
    result::ServiceResult<std::optional<model::LegacyResourceTransferBatch>> find(std::string id);
    result::ServiceResult<bool> cancel(std::string id);
    // 返回是否刷新了批次，观察lane据此决定是否退避。
    bool pollNext();
    result::ServiceResult<model::MetadataCoverPage> list(const model::MetadataCoverQuery &query) override;

private:
    result::ServiceResult<model::LegacyResourceTransferBatch> start(model::LegacyResourceTransferRequest request, bool sync);
    std::shared_ptr<ports::LegacyResourceTransferRepository> repository_;
    std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime_;
    std::shared_ptr<ports::ResourceTransferAccess> access_;
    std::shared_ptr<ports::MetadataCoverReader> cover_reader_;
    std::optional<model::ResourceDestination> cloud_provider_;
    bool filesystem_;
    int batch_timeout_ms_;
};

} // namespace orchestrator::modules::crawler::application::use_cases
