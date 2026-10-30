#pragma once

#include "crawler/application/model/ResourceProducerTask.h"
#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/result/ServiceResult.h"

#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全持久生成队列；登记产物、子操作和 producer 完成必须使用同一事务。
class ResourceProducerTaskRepository {
public:
    virtual ~ResourceProducerTaskRepository() = default;
    // 缺 key 只标记 waiting_input；恢复配置后可认领，版本不匹配不能偷换生成规则。
    virtual result::ServiceResult<std::optional<model::ResourceProducerTask>> claim(model::ResourceDestination destination, std::string fingerprint, std::string adapter, std::string processor_version, int schema_version, bool key_available) = 0;
    // 比较 producer 自身认领与来源/目标版本、父定向取消，不要求父仍 running。
    virtual result::ServiceResult<bool> shouldStop(const model::ResourceProducerTask &task) = 0;
    // 原子绑定来源参考、登记两个产物及本目标子操作；重放不得重复排队。
    virtual result::ServiceResult<std::string> complete(const model::ResourceProducerTask &task, const model::LyricArtifactBundle &bundle, std::uint64_t source_size) = 0;
    // 暂态错误按冻结次数/退避恢复，缺输入与解码失败保留可查询结果。
    virtual result::ServiceResult<bool> fail(const model::ResourceProducerTask &task, model::ResourceTransferJobFailure failure) = 0;
    // 崩溃认领恢复有界且推进 producer 自身代次，不重置父任务。
    virtual result::ServiceResult<int> recover(model::ResourceDestination destination, int stale_seconds) = 0;
    // 操作者明确重试失败/缺输入生成计划，仍核对原始范围和版本。
    virtual result::ServiceResult<bool> retry(std::uint64_t producer_id) = 0;
    // 按真实父操作返回有界的生成状态，不发起来源请求。
    virtual result::ServiceResult<std::vector<model::ResourceProducerTask>> list(std::string parent_operation_id) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
