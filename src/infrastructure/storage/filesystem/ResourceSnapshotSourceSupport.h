#pragma once
#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "crawler/application/model/ResourceTransferSource.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source {
// 租约只拥有直接创建的唯一子目录，配额归还不随 source adapter 销毁失效。
class Workspace {
public:
    // 创建并验证独占目录，调用者已预留本端配额。
    Workspace(std::filesystem::path base, std::shared_ptr<std::array<std::atomic<std::uint64_t>, 3>> reserved, std::size_t index, std::uint64_t bytes);
    // 只在已核对的父目录内清理本次目录并归还配额。
    ~Workspace();
    std::filesystem::path root;    // 独占目录
    std::uint8_t related_mask = 0; // 本项合法响应发现的种类，租约内不保存URL
    bool related_parsed = false;   // 区分未调用listen与明确没有关联资源
private:
    std::filesystem::path parent_;                                        // 规范化且已核对的父目录
    std::shared_ptr<std::array<std::atomic<std::uint64_t>, 3>> reserved_; // 保留计数
    std::size_t index_;                                                   // 本端索引
    std::uint64_t bytes_;                                                 // 预留量
};
// 复用歌曲流程与临时持久化会话，不使用原生数据库队列。
application::result::ServiceResult<std::string> song(const ResourceSnapshotDependencies &dependencies, const application::model::ResourceTransferJob &job, application::model::SongResourceQueueTarget target, const std::shared_ptr<Workspace> &workspace, const application::model::ResourceTransferContext &context);
// 正式 local 缺失时可使用共享生成内容或重新取得受控 MRC 私有快照。
application::result::ServiceResult<std::string> artifact(const ResourceSnapshotDependencies &dependencies, const application::model::ResourceArtifactInput &input, const std::shared_ptr<Workspace> &workspace, const application::model::ResourceTransferJob &job, const application::model::ResourceTransferContext &context);
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source
