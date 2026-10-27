#pragma once

#include "crawler/application/model/MetadataWriteBatch.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail {

// 单次事务状态；线程不安全，只能由所属协程顺序访问。
class MetadataWriteSession {
public:
    // 引用事务连接及只读模板，不创建或提交独立事务。
    MetadataWriteSession(boost::mysql::pooled_connection &connection, const std::map<std::string, std::string> &scripts);

    // 在事务内按明确且唯一的 Migu 物料映射归一输入，不按名称猜测身份。
    boost::asio::awaitable<void> normalizeAlbumIdentities(application::model::MetadataWriteBatch &batch);
    // 规范来源创建后迁入旧物料来源的关系与资源，再删除冗余来源。
    boost::asio::awaitable<void> mergeAlbumAliases();

    // 歌手实体和平台身份写入。
    boost::asio::awaitable<void> saveSinger(const application::model::SingerSourceMetadata &singer);
    // 来源声明总数只更新其来源行，与本响应实体事务一致。
    boost::asio::awaitable<void> saveSingerProfile(const application::model::SingerSourceProfile &profile);
    // 专辑实体和平台身份写入，身份不依赖展示名称。
    boost::asio::awaitable<void> saveAlbum(const application::model::AlbumSourceMetadata &album);
    // 歌曲实体和平台身份写入，不保存其他实体的属性。
    boost::asio::awaitable<void> saveSong(const application::model::SongSourceMetadata &song);
    // 两端实体已存在后才写入歌手分类归属。
    boost::asio::awaitable<void> saveMembership(const application::model::SingerIndexMembership &membership);
    // 歌手头像地址归入资源表，保留兼容调用显式提供的本地结果。
    boost::asio::awaitable<void> saveSingerResource(const application::model::SingerSourceMetadata &singer);
    // 专辑歌手关系和封面地址归入各自表，不执行下载。
    boost::asio::awaitable<void> saveAlbumRelations(const application::model::AlbumSourceMetadata &album);
    // 歌曲关系、音质和资源任务按表分步写入。
    boost::asio::awaitable<void> saveSongRelations(const application::model::SongSourceMetadata &song);

    // 返回同事务解析出的歌曲来源身份，不包含远端 URL。
    const std::vector<application::model::PersistedSongSource> &savedSongs() const
    {
        return saved_songs_;
    }

    // 仅在事务成功提交后清理已被替换或本事务未采用的新封面。
    const std::vector<std::string> &replacedCoverPaths() const
    {
        return replaced_cover_paths_;
    }

private:
    using SourceKey = std::pair<std::string, std::string>; // 平台内来源身份

    // 每个表级语句在单次事务内只 prepare 一次。
    boost::asio::awaitable<boost::mysql::statement> statement(const std::string &key);

    // 参数按值进入协程帧，避免异步执行时引用调用方临时对象。
    template <typename... Args>
    boost::asio::awaitable<std::uint64_t> write(std::string key, Args... args)
    {
        auto prepared = co_await statement(key);
        boost::mysql::results result;
        co_await connection_->async_execute(prepared.bind(args...), result, boost::asio::use_awaitable);
        co_return result.last_insert_id();
    }

    // 只用于输入未携带名称但来源身份已存在的关系端点。
    boost::asio::awaitable<std::uint64_t> singerId(const std::string &source, const std::string &id);
    // 查询已创建的专辑来源，不允许用可空关系替代缺失实体。
    boost::asio::awaitable<std::uint64_t> albumId(const std::string &source, const std::string &id);
    // 资源表共用参数形状，但每个 SQL 只写自己的表。
    boost::asio::awaitable<void> saveCover(const std::string &table, std::uint64_t owner, const application::model::MetadataResourceFile &cover, bool protect_detail = false, bool only_fill_missing = false);

    boost::mysql::pooled_connection &connection_;                      // 所属事务连接
    const std::map<std::string, std::string> &scripts_;                // 只读 SQL
    std::map<std::string, boost::mysql::statement> statements_;        // 本连接 prepared statements
    std::map<SourceKey, std::uint64_t> singer_ids_;                    // 已解析歌手来源主键
    std::map<SourceKey, std::uint64_t> album_ids_;                     // 已解析专辑来源主键
    std::map<SourceKey, std::uint64_t> song_ids_;                      // 已解析歌曲来源主键
    std::map<std::string, std::string> album_aliases_;                 // 本事务已核实的 Migu 物料 ID 到数字专辑 ID 映射
    std::vector<application::model::PersistedSongSource> saved_songs_; // 已保存歌曲的来源映射
    std::vector<std::string> replaced_cover_paths_;                    // 事务提交后可清理的旧文件或未采用新文件
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail
