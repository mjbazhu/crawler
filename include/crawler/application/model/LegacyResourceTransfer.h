#pragma once

#include "crawler/application/model/BatchProgress.h"
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/model/SongResourceQueue.h"

#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 旧入口的完成范围与真实操作分开冻结，不随后续平台配置变化。
struct LegacyResourceTransferRequest {
    ResourceOwner owner = ResourceOwner::Song;
    SongResourceClaimQuery songs;
    MetadataCoverOptions covers;
    std::optional<ResourceDestination> cloud_provider;   // 封面固定旧云端；歌曲旧filesystem仅local
    std::vector<ResourceTransferTargetProfile> profiles; // 本次实际统一执行目标
    bool strict_cloud = false;                           // 固定旧平台当前查询核验强度
    int batch_timeout_ms = 600000;                       // 旧批次总截止，排队和原必要后处理均计入
};

// 一个旧批次可映射多个有界真实操作，响应仍按逻辑资源计数。
struct LegacyResourceTransferBatch {
    std::string batch_id;
    ResourceOwner owner = ResourceOwner::Song;
    std::vector<std::string> operation_ids;
    BatchProgressSnapshot progress {};
    SongResourceProcessResult songs;
    std::vector<MetadataCoverItem> covers;
    bool terminal = false;
    bool timed_out = false; // 终态失败不能作为同步完成返回200
};

} // namespace orchestrator::modules::crawler::application::model
