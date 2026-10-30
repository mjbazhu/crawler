#pragma once

#include "crawler/application/model/CloudFile.h"

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 云端任务状态与上次成功回执分开，上传失败不损坏本地成功记录。
struct MetadataCoverCloud {
    std::string provider = "baidu";       // 兼容视图明确选择的平台，不由工具名称推断
    std::uint64_t content_generation = 0; // 当前资源内容代次，用于拒绝旧云端回写
    std::uint64_t target_revision = 0;    // 当前平台目标修订
    std::uint64_t source_revision = 0;    // 来源URL/身份变更的独立围栏
    CloudFileReceipt receipt;             // 上次成功云端对象
    int status = 0;                       // 0 待上传，1 运行中，2 已确认，3 失败，4 待核对
    std::uint64_t uploaded_at = 0;        // 最近确认时间
    std::uint64_t claim_generation = 0;   // 独立云端认领代次
    std::uint64_t claimed_at = 0;         // 独立云端认领时间
    std::string running_batch_id;         // 云端运行批次
    std::string error_category;           // 云端错误分类
    std::string result_code;              // 云端稳定结果码
    std::string last_error;               // 云端脱敏说明
    bool enabled = false;                 // 当前服务是否开启上传
    bool current = false;                 // 是否匹配当前文件和目标配置
};

} // namespace orchestrator::modules::crawler::application::model
