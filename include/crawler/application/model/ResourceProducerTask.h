#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 独立生成计划，父传输结束不会使此任务消失；来源仍由冻结资源身份重新取得。
struct ResourceProducerTask {
    std::uint64_t producer_id = 0;      // 持久生成任务 ID
    ResourceTransferJob source_job;     // 父 MRC/目标/内容/来源冻结身份，不使用父 running 作为执行前提
    std::string processor_version;      // 首次受理的解码/导出版本
    int schema_version = 1;             // 产物协议
    ResourceTransferJobPolicy policy;   // 独立预算，不消耗父下载预算
    std::uint64_t claim_generation = 0; // 只属于 producer 的认领代次
    std::uint64_t claimed_at = 0;       // 最近认领时间，Unix 秒
    int attempt_count = 0;              // 已认领次数
    std::string state;                  // waiting_input/pending/running/retry/succeeded/failed/cancelled/blocked
    std::string result_code;            // 脱敏稳定结果码
    bool cancel_requested = false;      // 当前目标取消
    std::string child_operation_id;     // 原子提交的产物操作 ID
};

} // namespace orchestrator::modules::crawler::application::model
