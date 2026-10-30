#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"

#include <memory>
#include <array>
#include <cstddef>

namespace orchestrator::modules::crawler::application::use_cases {

// 线程安全的有界全局配额；按等待目标轮转，不持锁调用取消或 I/O。
class ResourceTransferBudget {
public:
    class State; // 私有实现状态由 lease 共享，预算对象销毁不悬空
    // 不可复制的配额所有权，析构释放一次。
    class Lease {
    public:
        // 在已取得许可后保存共享状态。
        explicit Lease(std::shared_ptr<State> state, std::size_t destination);
        // 归还许可并唤醒等待者。
        ~Lease();
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;

    private:
        std::shared_ptr<State> state_; // 固定容量和等待队列
        std::size_t destination_;      // 本端占用归还，维持逐端保留配额
    };
    // 容量必须为 1..96；来源和传输分别创建实例。
    explicit ResourceTransferBudget(int capacity, std::array<bool, 3> reserved_targets = {});
    // 有界等待本目标许可；停止时返回空，不占用其他目标许可。
    std::unique_ptr<Lease> acquire(model::ResourceDestination destination, const model::ResourceTransferContext &context);

private:
    std::shared_ptr<State> state_; // 共享线程安全计数和等待状态
};

} // namespace orchestrator::modules::crawler::application::use_cases
