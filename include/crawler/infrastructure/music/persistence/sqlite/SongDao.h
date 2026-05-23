#pragma once

#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/domain/music/model/Song.h"
#include "crawler/infrastructure/music/persistence/sqlite/BaseDao.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class SongDao : public BaseDao<domain::Song>, public application::ports::SongRepository {
public:
    // 使用基类的构造函数
    explicit SongDao(ConnectionPool &pool);

    bool saveBatch(const std::vector<domain::Song> &songs) override;

    //  插入专辑id
    bool insertAlbumId(int64_t album_id, std::vector<domain::Song> &songs);

    // 根据歌手ID获取歌曲列表
    std::vector<domain::Song> getBySingerId(int64_t singer_id);

    // 根据歌手ID获取歌曲数量
    uint64_t countBySingerId(int64_t singer_id);

    // 根据专辑ID获取歌曲数量
    uint64_t countByAlbumId(int64_t album_id);

    domain::Song getBySongId(int64_t id);

protected:
    // 实现基类的纯虚函数
    bool doSave(PooledConnection &conn, const domain::Song &item) override;
    std::string getTableName() override;
    domain::Song mapRowToEntity(Statement &stmt) override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
