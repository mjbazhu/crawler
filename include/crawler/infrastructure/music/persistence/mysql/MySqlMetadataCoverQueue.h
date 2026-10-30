#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/application/ports/persistence/MetadataCoverQueue.h"
#include "crawler/application/ports/persistence/MetadataCoverCloudQueue.h"
#include "crawler/application/model/SongResourceFile.h"
#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>

#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 封面队列；线程安全，每次操作独占池连接，SQL 按表外置。
class MySqlMetadataCoverQueue final : public application::ports::MetadataCoverQueue, public application::ports::MetadataCoverCloudQueue {
public:
    // 保存线程安全连接池。
    explicit MySqlMetadataCoverQueue(std::shared_ptr<MySqlConnectionPool> pool, application::model::ResourceDestination legacy_cloud_provider = application::model::ResourceDestination::Baidu);
    // 查询分页及总数。
    application::result::ServiceResult<application::model::MetadataCoverPage> list(const application::model::MetadataCoverQuery &query) override;
    // 按资源身份查找。
    application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> find(application::model::CoverOwner owner, std::uint64_t id) override;
    // 原子条件认领。
    application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> claim(const application::model::MetadataCoverRecord &target, bool refresh, const std::string &batch_id) override;
    // 确认已提交图片回执，断线后重读事实。
    application::result::ServiceResult<bool> succeed(const application::model::MetadataCoverRecord &target, const application::model::MetadataResourceFile &file) override;
    // 条件结束失败并保留旧回执。
    application::result::ServiceResult<bool> fail(const application::model::MetadataCoverRecord &target, const application::model::MetadataResourceFile &error) override;
    // 恢复超时认领。
    application::result::ServiceResult<int> recover(const application::model::MetadataCoverQuery &query, int stale_seconds) override;
    // 以本地完整文件快照认领独立云端任务。
    application::result::ServiceResult<std::optional<application::model::MetadataCoverRecord>> claimCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &planned, const std::string &batch_id) override;
    // 文件与云端代次仍匹配时确认对象。
    application::result::ServiceResult<bool> succeedCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileReceipt &receipt) override;
    // 云端错误只修改云端状态。
    application::result::ServiceResult<bool> failCloud(const application::model::MetadataCoverRecord &target, const application::model::CloudFileResult &error) override;
    // 恢复超时云端认领。
    application::result::ServiceResult<int> recoverCloud(const application::model::MetadataCoverQuery &query, int stale_seconds) override;
    // 启动恢复时按所有者、代次和实际文件判断日志去留。
    std::optional<application::model::ResourceWriteDisposition> inspectWrite(application::model::CoverOwner owner, std::uint64_t id, std::uint64_t generation, const std::string &path);

private:
    std::shared_ptr<MySqlConnectionPool> pool_;              // 连接生命周期由组合根管理
    application::model::ResourceDestination cloud_provider_; // 兼容单端视图的明确平台
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
