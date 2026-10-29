#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/ports/persistence/MetadataCoverQueue.h"
#include "crawler/application/ports/storage/MetadataCoverReader.h"
#include "crawler/application/ports/storage/MetadataCoverStore.h"

#include <memory>

#include <functional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 线程安全的单资源用例；只编排认领、下载和文件/数据库确认。
class MetadataCoverDownloadWorkflow {
public:
    // 保存线程安全 port，无任务内共享可变状态。
    MetadataCoverDownloadWorkflow(std::shared_ptr<ports::MetadataCoverQueue> queue, std::shared_ptr<ports::MetadataCoverStore> store, std::shared_ptr<ports::MetadataCoverReader> reader);
    // 每次调用处理一条已查询资源，取消与重试均保留旧文件。
    model::MetadataCoverItem process(model::MetadataCoverRecord target, const model::MetadataCoverOptions &options, const std::string &batch_id = {}, std::function<bool()> cancelled = {});

private:
    std::shared_ptr<ports::MetadataCoverQueue> queue_;   // 条件认领与回写
    std::shared_ptr<ports::MetadataCoverStore> store_;   // 下载及文件补偿
    std::shared_ptr<ports::MetadataCoverReader> reader_; // 检查旧文件
};

} // namespace orchestrator::modules::crawler::application::use_cases
