#pragma once
#include "crawler/application/model/ResourceDiscovery.h"
#include "crawler/application/result/ServiceResult.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace orchestrator::modules::crawler::application::ports {
// 线程安全的发现任务与页检查点，不进行网络或文件操作。
class ResourceDiscoveryRepository {
public:
    // 释放仓储。
    virtual ~ResourceDiscoveryRepository() = default;
    // 同ID相同定义重放，异定义或同目标另一活动run拒绝。
    virtual result::ServiceResult<model::ResourceDiscoveryRun> start(model::ResourceDiscoveryDefinition definition) = 0;
    // 查询固定范围和已提交计数。
    virtual result::ServiceResult<model::ResourceDiscoveryRun> find(std::string id) = 0;
    // 一页有界租约；过期重认领推进代次。
    virtual result::ServiceResult<std::optional<model::ResourceDiscoveryRun>> claim(std::string id) = 0;
    // 当前租约/目标是否仍有效，不修改状态。
    virtual result::ServiceResult<bool> current(const model::ResourceDiscoveryRun &lease) = 0;
    // 同事务写观察和游标；未决提交保留供重读。
    virtual result::ServiceResult<bool> commit(const model::ResourceDiscoveryRun &lease, const model::LegacyResourceImportPage &page, std::vector<model::ResourceDiscoveryObservation> observations) = 0;
    // 暂停/恢复/取消；不重置已提交游标。
    virtual result::ServiceResult<model::ResourceDiscoveryRun> control(std::string id, std::string action) = 0;
    // 故障或页截止暂停，仅允许当前租约回写。
    virtual result::ServiceResult<bool> release(const model::ResourceDiscoveryRun &lease, std::string code) = 0;
    // 读取已提交观察；upper固定导出快照游标。
    virtual result::ServiceResult<std::vector<model::ResourceDiscoveryObservation>> observations(std::string id, std::uint64_t after, std::uint64_t upper, std::size_t limit) = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
