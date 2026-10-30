#pragma once
#include "migu/support/MetadataCoverMySqlTestSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/application/model/ResourceTransferJobs.h"
#include <stdexcept>
#include <string>
namespace orchestrator::modules::crawler::tests::archive {
namespace mysql = infrastructure::music::persistence::mysql;
namespace model = application::model;
// 固定断言消息，不输出连接或云端原始值。
inline void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 只在调用方已校验的隔离库运行fixture。
inline boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return migu::support::runOnExecutor(pool->executor(), pool->coExecute(std::move(text)), "archive fixture");
}
// 单个非空整数结果。
inline std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return mysql::MySqlValueReader::uint64At(execute(pool, std::move(text)).rows().front(), 0);
}
// 仅保留仓储的固定诊断。
template <class T>
T get(application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 独立来源名称不与其他fixture重复。
inline std::uint64_t singer(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, int index)
{
    model::SingerSourceMetadata singer;
    singer.source = "migu";
    singer.source_singer_id = "archive-" + std::to_string(index);
    singer.name = singer.source_singer_id;
    singer.avatar_url = "https://example.test/avatar.png";
    model::MetadataWriteBatch batch;
    batch.singers = {singer};
    get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
    return scalar(pool, "SELECT MAX(id) FROM singer_resources");
}
// 歌曲只写MRC元数据，不请求内容。
inline std::uint64_t mrc(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, int index)
{
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "archive-" + std::to_string(index);
    song.source_content_id = song.source_song_id;
    song.title = song.source_song_id;
    model::MetadataWriteBatch batch;
    batch.songs = {song};
    get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind) SELECT id,'mrc' FROM song_source_refs WHERE source_song_id='" + song.source_song_id + "'");
    return scalar(pool, "SELECT MAX(id) FROM song_resources");
}
// 三端不同身份，百度以rclone代表WebDAV可选适配。
inline std::vector<model::ResourceTransferTargetProfile> profiles(bool producer = false)
{
    std::vector<model::ResourceTransferTargetProfile> values;
    for (auto dest : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
        model::ResourceTransferTargetProfile v;
        v.destination = dest;
        v.adapter = dest == model::ResourceDestination::Local ? "filesystem" : "rclone";
        v.fingerprint = std::string(64, char('a' + static_cast<int>(dest)));
        v.produces_artifacts = producer;
        v.producer_version = "fixture-v1";
        v.producer_key_available = true;
        v.producer_policy = {5000, 1, 1024 * 1024, 0};
        values.push_back(v);
    }
    return values;
}
// 显式测试闸门，不在生产执行。
inline void activate(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64),target_fingerprint=CASE destination WHEN 'local' THEN REPEAT('a',64) WHEN 'baidu' THEN REPEAT('b',64) ELSE REPEAT('c',64) END");
}
// 请求身份及目标均显式选择。
inline model::ResourceTransferRequest request(model::ResourceOwner owner, std::uint64_t id, std::string key, std::vector<model::ResourceDestination> destinations = {model::ResourceDestination::Local})
{
    model::ResourceTransferRequest v;
    v.resources = {{owner, id}};
    v.idempotency_key = std::move(key);
    v.destinations = std::move(destinations);
    return v;
}
} // namespace orchestrator::modules::crawler::tests::archive
