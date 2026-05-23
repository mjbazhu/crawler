#pragma once

#include "crawler/application/ports/persistence/AlbumRepository.h"
#include "crawler/domain/music/model/Album.h"
#include "crawler/infrastructure/music/persistence/sqlite/BaseDao.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class AlbumDao : public BaseDao<domain::Album>, public application::ports::AlbumRepository {
public:
    explicit AlbumDao(ConnectionPool &pool);

    bool saveBatch(const std::vector<domain::Album> &albums) override;

    // 标记专辑已保存
    bool markDone(const domain::Album &item);

    // 判断专辑是否已保存
    bool isDone(const domain::Album &item);

    // 根据歌手ID获取专辑列表
    std::vector<domain::Album> getBySingerId(int64_t singer_id);

    // 根据歌手ID获取专辑数量
    uint64_t countBySingerId(int64_t singer_id);

protected:
    bool doSave(PooledConnection &conn, const domain::Album &item) override;
    std::string getTableName() override;
    domain::Album mapRowToEntity(Statement &stmt) override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
