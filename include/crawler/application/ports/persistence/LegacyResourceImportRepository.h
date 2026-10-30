#pragma once

#include "crawler/application/model/LegacyResourceImport.h"
#include "crawler/application/result/ServiceResult.h"

#include <cstddef>
#include <cstdint>

namespace orchestrator::modules::crawler::application::ports {

// 人工历史接管 port；预览只读，写入限于已审阅且目标维护中的有界页。
class LegacyResourceImportRepository {
public:
    // 释放导入器。
    virtual ~LegacyResourceImportRepository() = default;
    // 捕获数据库身份、目标及资源高水位页，不取得文件或云端内容。
    virtual result::ServiceResult<model::LegacyResourceImportPage> preview(model::ResourceOwner owner, std::uint64_t after_id, std::uint64_t high_water_id, std::size_t limit) = 0;
    // 同事务写双端声明、本地策略、证据与游标；不创建传输任务。
    virtual result::ServiceResult<model::LegacyResourceImportResult> importPage(model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage page) = 0;
    // 读取同清单双端一致游标，续跑不重新写已提交的旧页。
    virtual result::ServiceResult<model::LegacyResourceImportProgress> progress(model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage scope) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
