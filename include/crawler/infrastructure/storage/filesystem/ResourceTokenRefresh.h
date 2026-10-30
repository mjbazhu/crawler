#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
// 每个来源端独立的有界刷新协调器；仅内存保留时点，不持久化token。
class ResourceTokenRefresh {
public:
    // 构造不读取配置或请求网络。
    ResourceTokenRefresh();
    // 绑定本项上下文，周期刷新合并；明确拒绝后的force仍执行。
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> bind(const application::model::ResourceTransferJob &job, std::shared_ptr<application::ports::MiguPacmtokenRefresher> inner, std::shared_ptr<application::ports::MiguPacmtokenStore> store, const application::model::ResourceTransferContext &context) const;
    // 每个操作的首次有效音频尝试强制刷新一次，interval=0也保持旧合同。
    void initial(const std::shared_ptr<application::ports::MiguPacmtokenRefresher> &bound) const;

private:
    friend class ResourceBoundTokenRefresh;
    class State;
    std::shared_ptr<State> state_; // 等待者可取消，不取消其他项的网络请求
};
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
