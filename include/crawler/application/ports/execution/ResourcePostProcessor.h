#pragma once

#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/application/result/ServiceResult.h"

#include <memory>

namespace orchestrator::modules::crawler::application::ports {

// 已提交资源的可选后处理 port；由受限 blocking executor 调用，须可并发使用。
class ResourcePostProcessor {
public:
    // 经由抽象释放后处理用例。
    virtual ~ResourcePostProcessor() = default;
    // true 表示产物已登记，false 表示本处理器不适用；错误不撤销下载成功。
    virtual result::ServiceResult<bool> process(const model::SongResourceProcessItem &item) const = 0;
};

// 资源后处理依赖组，后续扩展不继续增加下载构造函数的独立参数。
struct ResourceProcessingExtensions {
    std::shared_ptr<ResourcePostProcessor> post_processor; // 可选产物处理器
};

} // namespace orchestrator::modules::crawler::application::ports
