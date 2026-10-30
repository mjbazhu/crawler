#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include <array>
#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {
// 四类清单按 song/singer/album/song_artifact 固定排列，空表也需要完成的空清单。
struct ResourceBaselinePlan {
    std::string baseline_id;                                         // 封存标识，不是资源传输操作 ID
    std::array<std::string, 4> import_runs;                          // 已完成的双端声明或排除 run，不含 provider 后缀
    bool writers_stopped = false;                                    // 操作者明确确认全部旧写入器已停止
    std::array<std::array<std::string, 2>, 4> cloud_supplement_runs; // 可选逐owner/baidu,gdrive单端历史补充，不替代local范围锚点
};
// 审阅摘要只包含数据库/清单/版本及计数，不包含认证和资源路径。
struct ResourceBaselineReview {
    std::string digest;                  // 完整审核快照 SHA-256，seal/activate 均重新比较
    std::uint64_t resources = 0;         // 四类已审阅历史行
    std::uint64_t blocked_resources = 0; // 未被历史保存声明覆盖的行，永久保留本代次阻止策略
    std::string state;                   // reviewed/sealed/active
};
} // namespace orchestrator::modules::crawler::application::model
