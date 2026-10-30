#pragma once
#include "crawler/application/model/SongResourceFile.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 线程安全的四类本地 journal 核对；未知和未决任务均保留对象。
class MySqlResourceWriteInspector {
public:
    // 共享已初始化连接池。
    explicit MySqlResourceWriteInspector(std::shared_ptr<MySqlConnectionPool> pool);
    // 已引用路径先于原生任务状态判断，防止刷新或修复删掉最后确认文件。
    std::optional<application::model::ResourceWriteDisposition> inspect(std::string owner, std::uint64_t resource, std::uint64_t generation, std::string path);

private:
    std::shared_ptr<MySqlConnectionPool> pool_; // 每次核对独占连接
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
