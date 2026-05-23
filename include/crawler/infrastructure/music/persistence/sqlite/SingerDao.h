#pragma once

#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/domain/music/model/Singer.h"
#include "crawler/infrastructure/music/persistence/sqlite/BaseDao.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class SingerDao : public BaseDao<domain::Singer>, public application::ports::SingerRepository {
public:
    explicit SingerDao(ConnectionPool &pool);

    bool saveBatch(const std::vector<domain::Singer> &singers) override;

    // 标记歌手已保存
    bool markDone(int64_t singer_id);

    // 判断歌手是否已保存
    bool isDone(int64_t singer_id);

    // 获取artwork
    std::string getArtwork(int64_t singer_id);

    // 获取歌手信息
    std::vector<domain::Singer> getBySingerId(int64_t singer_id);

    // 获取name
    std::vector<std::string> getNameByStatus();

protected:
    bool doSave(PooledConnection &conn, const domain::Singer &item) override;
    std::string getTableName() override;
    domain::Singer mapRowToEntity(Statement &stmt) override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
