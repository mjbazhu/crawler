#pragma once

#include "crawler/application/model/LegacyResourceTransfer.h"
#include "crawler/application/result/ServiceResult.h"

namespace orchestrator::modules::crawler::application::ports {

// 旧视图与统一任务的持久桥接；不执行文件或网络传输。
class LegacyResourceTransferRepository {
public:
    virtual ~LegacyResourceTransferRepository() = default;
    // 选择旧范围、分块登记统一操作和旧批次映射在同一事务完成。
    virtual result::ServiceResult<model::LegacyResourceTransferBatch> start(model::LegacyResourceTransferRequest request) = 0;
    // 刷新固定范围的结果；终态结果保持不可变，无此旧批次返回空。
    virtual result::ServiceResult<std::optional<model::LegacyResourceTransferBatch>> find(std::string batch_id) = 0;
    // 原子取消该旧批次实际拥有的全部统一操作；终态返回Conflict，未知返回NotFound。
    virtual result::ServiceResult<bool> cancel(std::string batch_id) = 0;
    // 按持久游标轮转一个活跃旧批次，供低频观察lane跨重启恢复投影。
    virtual result::ServiceResult<bool> pollNext() = 0;
    virtual result::ServiceResult<model::MetadataCoverPage> listCovers(model::LegacyResourceTransferRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
