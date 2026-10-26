#pragma once

#include "crawler/application/model/MetadataWriteBatch.h"
#include "crawler/application/result/ServiceResult.h"

namespace orchestrator::modules::crawler::application::ports {

// 元数据事务写入 port；实现必须先创建实体，再建立关系，不执行网络或文件操作。
class MetadataBatchWriter {
public:
    // 释放实现持有的基础设施资源。
    virtual ~MetadataBatchWriter() = default;

    // 原子保存整个批次；任意实体或关系写入失败时回滚。
    virtual result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
