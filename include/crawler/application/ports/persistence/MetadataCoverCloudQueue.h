#pragma once

#include "crawler/application/model/CloudFile.h"
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/result/ServiceResult.h"

#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 云端认领与确认 port；线程安全，独立于本地下载状态迁移。
class MetadataCoverCloudQueue {
public:
    // 释放队列。
    virtual ~MetadataCoverCloudQueue() = default;
    // 重读当前文件与云端代次。
    virtual result::ServiceResult<std::optional<model::MetadataCoverRecord>> find(model::CoverOwner owner, std::uint64_t id) = 0;
    // 对已提交文件的快照认领云端任务。
    virtual result::ServiceResult<std::optional<model::MetadataCoverRecord>> claimCloud(const model::MetadataCoverRecord &target, const model::CloudFileReceipt &planned, const std::string &batch_id) = 0;
    // 只有代次与当前文件都匹配才确认云端回执。
    virtual result::ServiceResult<bool> succeedCloud(const model::MetadataCoverRecord &target, const model::CloudFileReceipt &receipt) = 0;
    // 结束失败或待核对状态，保留本地及上次成功云端回执。
    virtual result::ServiceResult<bool> failCloud(const model::MetadataCoverRecord &target, const model::CloudFileResult &error) = 0;
    // 恢复超时云端认领，并使旧 worker 失效。
    virtual result::ServiceResult<int> recoverCloud(const model::MetadataCoverQuery &query, int stale_seconds) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
