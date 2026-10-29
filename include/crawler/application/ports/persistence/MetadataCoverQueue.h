#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/application/result/ServiceResult.h"

#include <optional>
#include <string>

#include <cstdint>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全的封面队列 port；数据库异常与未命中必须分别返回。
class MetadataCoverQueue {
public:
    // 释放队列。
    virtual ~MetadataCoverQueue() = default;
    // 分页查询；不包含文件 I/O。
    virtual result::ServiceResult<model::MetadataCoverPage> list(const model::MetadataCoverQuery &query) = 0;
    // 按类型与主键查询资源。
    virtual result::ServiceResult<std::optional<model::MetadataCoverRecord>> find(model::CoverOwner owner, std::uint64_t id) = 0;
    // 仅在状态、代次和 URL 快照仍匹配时认领，空值表示已被其他任务处理。
    virtual result::ServiceResult<std::optional<model::MetadataCoverRecord>> claim(const model::MetadataCoverRecord &target, bool refresh, const std::string &batch_id) = 0;
    // 完整文件提交后确认回写；failure 表示提交结果仍无法核实。
    virtual result::ServiceResult<bool> succeed(const model::MetadataCoverRecord &target, const model::MetadataResourceFile &file) = 0;
    // 结束本代次失败；旧成功文件回执必须保留。
    virtual result::ServiceResult<bool> fail(const model::MetadataCoverRecord &target, const model::MetadataResourceFile &error) = 0;
    // 恢复超时认领并使旧 worker 失效。
    virtual result::ServiceResult<int> recover(const model::MetadataCoverQuery &query, int stale_seconds) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
