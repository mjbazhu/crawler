#pragma once

#include "crawler/application/ports/execution/ResourceTransferRuntimeControl.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::app {

// 每条路只调用自己的 worker；回调返回是否处理了一项，停止检查只取消本次尝试。
struct ResourceRuntimeLane {
    application::model::ResourceDestination destination; // 平台/本地身份
    int concurrency = 1;                                 // 固定 worker 数，运行期不强杀已有任务
    std::function<bool(std::function<bool()>)> run_one;  // 拥有 worker 及其全部依赖
    std::string kind = "transfer";                       // transfer/producer 分别占用独立 worker
};

// 线程安全的调度生命周期；宽限到期保留工作依赖和持久未决状态，不借析构伪造完成。
class ResourceTransferRuntime final : public application::ports::ResourceTransferRuntimeControl {
public:
    class State; // worker 共享所有权，退出期间不引用 runtime 裸指针
    // 配置已核验；构造不启动传输。
    ResourceTransferRuntime(bool parallel, std::vector<application::model::ResourceTransferTargetProfile> profiles, std::vector<ResourceRuntimeLane> lanes, int shutdown_grace_ms);
    // 停止新认领并请求当前工作取消，晚到 worker 自持依赖。
    ~ResourceTransferRuntime();
    // 只启动一次；应在路由与所有依赖装配成功后调用。
    void start();
    // 路由装配后、start 前添加只读/投影观察任务；不受某个传输目标关闭影响。
    void addObserver(std::function<bool()> poll);
    // 停止新认领，宽限内排空；到期请求协作取消，返回是否已实际排空。
    bool shutdown();
    bool parallelEnabled() const override;
    std::vector<application::model::ResourceTransferTargetProfile> profiles() const override;
    application::result::ServiceResult<bool> updatePolicy(application::model::ResourceDestination destination, application::model::ResourceTransferJobPolicy policy) override;

private:
    std::shared_ptr<State> state_; // 配置快照、控制标志和独立 worker 句柄
};

} // namespace orchestrator::modules::crawler::infrastructure::app
