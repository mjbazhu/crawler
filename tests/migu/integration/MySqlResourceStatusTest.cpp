#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace {
// 固定短诊断，不回显连接或内容。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 测试SQL仅作用于显式隔离库。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(text)), "status fixture");
}
// 读取fixture生成身份。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return mysql::MySqlValueReader::uint64At(execute(pool, std::move(text)).rows().front(), 0);
}
// 用例失败保留脱敏业务信息。
template <class T>
T get(c::application::result::ServiceResult<T> result)
{
    if (!result.isSuccess()) throw std::runtime_error(result.message());
    return *result.data();
}
} // namespace
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_status_"), "refusing non-isolated status database");
        auto pool = support::makeCoverTestPool();
        model::MetadataWriteBatch seed;
        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "status-song";
        song.source_content_id = "status-content";
        song.title = "批量状态";
        song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
        seed.songs = {song};
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "status-singer";
        singer.name = "状态歌手";
        singer.avatar_url = "https://example.test/avatar.png";
        seed.singers = {singer};
        model::AlbumSourceMetadata album;
        album.source = "migu";
        album.source_album_id = "status-album";
        album.title = "状态专辑";
        album.cover_url = "https://example.test/cover.png";
        seed.albums = {album};
        require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(seed).isSuccess(), "status metadata fixture failed");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind) SELECT id,'mrc' FROM song_source_refs");
        const auto source = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc'");
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,schema_version) VALUES(" + std::to_string(source) + ",'word_json',REPEAT('a',64),'fixture',1)");
        const std::vector<std::string> tables = {"song_resources", "singer_resources", "album_resources", "song_resource_artifacts"};
        for (const auto &table : tables) execute(pool, "UPDATE " + table + " SET status=2,local_path='fixture/file',checksum=REPEAT('a',64),size_bytes=3,reference_sha256=REPEAT('a',64),reference_size_bytes=3,local_content_generation=content_generation,local_target_revision=1");
        std::vector<model::ResourceIdentity> ids;
        for (auto owner : {model::ResourceOwner::Song, model::ResourceOwner::Singer, model::ResourceOwner::Album, model::ResourceOwner::SongArtifact}) {
            const auto ordinal = static_cast<std::size_t>(owner);
            const auto id = scalar(pool, "SELECT MIN(id) FROM " + tables[ordinal]);
            ids.push_back({owner, id});
            const auto name = std::string(model::resourceOwnerName(owner));
            const auto fk = owner == model::ResourceOwner::Song ? "song_resource_id" : owner == model::ResourceOwner::Singer ? "singer_resource_id" :
                                                                                   owner == model::ResourceOwner::Album      ? "album_resource_id" :
                                                                                                                               "artifact_resource_id";
            for (const auto provider : {"baidu", "gdrive"}) execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + std::string(fk) + ",source_adapter,task_state,confirmed_content_generation,confirmed_target_revision,size_bytes,checksum,verification,verification_method,cloud_path,path_unresolved) VALUES('" + name + "'," + std::to_string(id) + ",'" + provider + "'," + std::to_string(id) + ",'rclone','succeeded',1,1,3,REPEAT('a',64),'sha256_verified','fixture','fixture/object',0)");
        }
        mysql::MySqlResourceTransferQueries queries(pool);
        require(get(queries.compatible()), "status schema incompatible");
        model::ResourceStatusQuery query;
        query.resources = {ids[2], ids[0], ids[3], ids[1], {model::ResourceOwner::Song, 999999}};
        const auto batch = get(queries.status(query));
        require(batch.items.size() == 5 && batch.requested_targets.size() == 3, "status batch changed cardinality");
        for (std::size_t i = 0; i < 4; ++i) require(batch.items[i].resource == query.resources[i] && batch.items[i].found && batch.items[i].local.current && batch.items[i].cloud_receipts.size() == 2 && batch.items[i].cloud_receipts[0].current && batch.items[i].cloud_receipts[1].current, "four-owner facts or input order mismatch");
        require(!batch.items[4].found && !batch.items[4].local.current && batch.items[4].cloud_receipts.empty(), "missing resource fabricated evidence");
        execute(pool, "UPDATE singer_resources SET checksum='md5:0123456789abcdef0123456789abcdef',reference_sha256='',local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='reviewed',local_skip_reason='both_saved'");
        execute(pool, "UPDATE resource_cloud_receipts SET verification='operator_attested',transfer_policy='skip',policy_content_generation=1,policy_target_revision=1,baseline_id='reviewed',skip_reason='both_saved' WHERE owner_type='singer'");
        auto history = get(queries.status({{ids[1]}, {}})).items.front();
        require(!history.local.current && history.local.transfer_skipped && history.cloud_receipts[0].assumed_stored && history.cloud_receipts[0].transfer_skipped && !history.cloud_receipts[0].current, "legacy digest or historical skip became verification");
        execute(pool, "UPDATE song_resources SET status=3 WHERE resource_kind='audio'");
        execute(pool, "UPDATE resource_cloud_receipts SET task_state='failed' WHERE owner_type='song'");
        const auto retained = get(queries.status({{ids[0]}, {}})).items.front();
        require(retained.local.current && retained.cloud_receipts[0].current, "later failure erased retained confirmation");
        execute(pool, "UPDATE resource_destination_states SET target_revision=2 WHERE destination='local'");
        require(!get(queries.status({{ids[0]}, {}})).items.front().local.current, "old local target remained current");
        execute(pool, "UPDATE resource_destination_states SET target_revision=2 WHERE destination='baidu'");
        const auto changed = get(queries.status({{ids[0]}, {}})).items.front();
        require(!changed.cloud_receipts[0].current && changed.cloud_receipts[1].current, "target revision crossed platforms");
        query.resources.push_back(query.resources[0]);
        require(queries.status(query).error() == c::application::result::ServiceErrorType::InvalidParam, "duplicate identity accepted");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 0, "read created transfer work");
        std::cout << "PASS: one bounded snapshot, four owners, ordered missing rows, local/cloud evidence, skip, retained confirmation and target revisions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "status test failed: " << error.what() << '\n';
        return 1;
    }
}
