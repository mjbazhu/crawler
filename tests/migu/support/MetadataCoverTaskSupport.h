#pragma once

#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/execution/TaskExecutor.h"

#include <map>
#include <vector>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::tests::cover {

// 手动推进的有界任务替身，用于确定地检查取消和延迟认领。
class Executor final : public application::ports::TaskExecutor {
public:
    std::vector<std::function<void()>> pending; // 尚未开始的 worker
    // 保存 worker，等待测试推进。
    void submit(std::function<void()> task) override
    {
        pending.push_back(std::move(task));
    }
    // 执行当前所有 worker。
    void drain()
    {
        auto tasks = std::move(pending);
        pending.clear();
        for (auto &task : tasks) task();
    }
};

// 记录真实批次协议的内存替身，不访问数据库。
class Batches final : public application::ports::BatchTaskRepository {
public:
    std::map<std::string, application::model::CrawlBatchRecord> records; // 批次结果
    std::vector<application::model::CrawlBatchItemRecord> items;         // 所有者与目标
    std::vector<application::model::CrawlBatchEventRecord> events;       // 进度事件
    bool reject_create = false;                                          // 模拟批次写入失败
    // 原子保存批次与输入目标。
    bool createBatch(const application::model::CrawlBatchRecord &record, const std::vector<application::model::CrawlBatchItemRecord> &targets) override
    {
        if (reject_create) return false;
        records[record.batch_id] = record;
        items.insert(items.end(), targets.begin(), targets.end());
        return true;
    }
    // 只更新进度，保留创建时的请求快照。
    bool updateBatchProgress(const application::model::CrawlBatchRecord &record) override
    {
        auto &current = records[record.batch_id];
        current.state = record.state;
        current.processed_count = record.processed_count;
        current.success_count = record.success_count;
        current.failure_count = record.failure_count;
        return true;
    }
    // 保存单个 item 最终状态。
    bool updateItem(const application::model::CrawlBatchItemRecord &record) override
    {
        for (auto &item : items)
            if (item.batch_id == record.batch_id && item.item_index == record.item_index) {
                item = record;
                return true;
            }
        return false;
    }
    // 本测试不动态发现目标。
    bool upsertItem(const application::model::CrawlBatchItemRecord &record) override
    {
        return updateItem(record);
    }
    // 保留事件供 JSON 与身份断言。
    bool appendEvent(const application::model::CrawlBatchEventRecord &event) override
    {
        events.push_back(event);
        return true;
    }
    // 本测试只查询具体批次。
    application::model::CrawlBatchPage listBatches(const application::model::CrawlBatchQuery &) override
    {
        return {};
    }
    // 返回已保存批次。
    std::optional<application::model::CrawlBatchRecord> findBatch(std::string id) override
    {
        const auto it = records.find(id);
        return it == records.end() ? std::nullopt : std::optional(it->second);
    }
    // 本测试直接核对记录集合。
    application::model::CrawlBatchItemPage listItems(std::string, const application::model::CrawlBatchItemQuery &) override
    {
        return {};
    }
    // 本测试直接核对事件集合。
    application::model::CrawlBatchEventPage listEvents(std::string, const application::model::CrawlBatchEventQuery &) override
    {
        return {};
    }
    // 本测试不统计全局任务。
    application::model::CrawlBatchSummary summary(std::string, int) override
    {
        return {};
    }
    // 本测试不归档任务。
    bool archiveBatch(std::string, std::string) override
    {
        return false;
    }
    // 本测试不恢复其他类型任务。
    int recoverStaleRunningBatches(std::string, int, std::string) override
    {
        return 0;
    }
};

} // namespace orchestrator::modules::crawler::tests::cover
