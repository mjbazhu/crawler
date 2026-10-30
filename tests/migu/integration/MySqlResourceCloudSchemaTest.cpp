#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"
#include "migu/support/RetiredResourceCloudTestSupport.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace sql = c::infrastructure::music::persistence::sql;

namespace {
// 限定固定测试断言，避免输出连接配置或业务内容。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 已校验隔离库内执行固定 fixture SQL。
void execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    (void)support::runOnExecutor(pool->executor(), pool->coExecute(std::move(statement)), "receipt schema fixture");
}

// 读取测试专属计数或身份。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string statement)
{
    auto result = support::runOnExecutor(pool->executor(), pool->coExecute(std::move(statement)), "receipt schema scalar");
    require(!result.rows().empty(), "scalar returned no row");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 非法写入必须由数据库拒绝，而非只由上层参数校验拒绝。
void rejected(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &statement)
{
    bool failed = false;
    try {
        execute(pool, statement);
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "invalid receipt identity was accepted by MySQL");
}

// 使用正式分句器在同一连接重跑完整扩展迁移。
boost::asio::awaitable<bool> replay(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    for (const auto path : {"sql/mysql/migrations/V2026102902__resource_cloud_receipts_expand.sql", "sql/mysql/migrations/V2026102911__resource_discovery_and_queue_capacity.sql", "sql/mysql/migrations/V2026102912__history_adoption_controls_and_cleanup.sql"})
        for (const auto &statement : sql::SqlFileLoader::splitMigrationStatements(sql::SqlFileLoader::loadOrThrow(path)))
            co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    co_return co_await mysql::MySqlMigrationRunner::coRun(connection, "sql/mysql/migrations");
}

// 用原历史文件初始化 V2901，再用正式 runner 升级，保留可复核的独立迁移目录。
std::shared_ptr<mysql::MySqlConnectionPool> legacyPool(mysql::MySqlConfig config)
{
    boost::asio::io_context context;
    require(support::runInIoContext(context, support::createCoverTestDatabase(config), "create legacy receipt database"), "legacy database creation failed");
    const auto directory = std::filesystem::temp_directory_path() / (config.database + "_before_receipts");
    std::filesystem::create_directories(directory);
    for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations"))
        if (entry.path().extension() == ".sql" && entry.path().filename().string() < "V2026102902") std::filesystem::copy_file(entry.path(), directory / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
    config.migrations_path = directory.string();
    auto pool = std::make_shared<mysql::MySqlConnectionPool>(std::move(config));
    require(support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "initialize legacy receipt schema"), "legacy receipt schema failed");
    return pool;
}

// 正式迁移 runner 核对历史 checksum 后安装扩展，而非只手工执行新 DDL。
boost::asio::awaitable<bool> upgrade(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    co_return co_await mysql::MySqlMigrationRunner::coRun(connection, "sql/mysql/migrations");
}
} // namespace

// 真实隔离 MySQL 验证扩展、默认关闭、四类归属、重跑及级联，不请求任何音乐/网盘服务。
int main(int argc, char **argv)
{
    try {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_receipts_"), "refusing non-isolated receipt schema database");
        const bool from_legacy = argc == 2 && std::string_view(argv[1]) == "--upgrade-from-legacy";
        require(argc == 1 || from_legacy, "unsupported receipt schema test option");
        auto pool = from_legacy ? legacyPool(config) : support::makeCoverTestPool();
        require(scalar(pool, "SELECT (SELECT COUNT(*) FROM songs)+(SELECT COUNT(*) FROM singers)+(SELECT COUNT(*) FROM albums)") == 0, "receipt schema test requires empty business tables");
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        if (from_legacy) require(!catalog.schemaStatus().ok, "legacy schema was prematurely ready");

        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "receipt-schema-singer";
        singer.name = "回执结构歌手";
        singer.avatar_url = "https://example.test/singer.png";
        model::AlbumSourceMetadata album;
        album.source_album_id = "receipt-schema-album";
        album.title = "回执结构专辑";
        album.source_album_kind = 1;
        album.source_resource_type = "2003";
        album.cover_url = "https://example.test/album.png";
        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "receipt-schema-song";
        song.source_content_id = "receipt-schema-content";
        song.title = "回执结构歌曲";
        song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
        model::MetadataWriteBatch batch;
        batch.singers = {singer};
        batch.albums = {album};
        batch.songs = {song};
        mysql::MySqlMetadataBatchWriter writer(pool);
        require(writer.saveBatch(batch).isSuccess(), "receipt schema fixtures failed");
        execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT source.id,'mrc','' FROM song_source_refs source WHERE source.source_content_id='receipt-schema-content' ON DUPLICATE KEY UPDATE id=song_resources.id");
        const auto mrc = scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc' LIMIT 1");
        execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,local_path,size_bytes,checksum,status) VALUES(" + std::to_string(mrc) + ",'word_json',REPEAT('a',64),'fixture','words.json',1,REPEAT('b',64),2)");
        const auto artifact = scalar(pool, "SELECT id FROM song_resource_artifacts LIMIT 1");
        const auto singer_id = scalar(pool, "SELECT id FROM singer_resources LIMIT 1");
        const auto album_id = scalar(pool, "SELECT id FROM album_resources LIMIT 1");

        execute(pool, "UPDATE song_resources SET status=2,local_path='missing/legacy.flac',checksum='rclone:size:123' WHERE id=" + std::to_string(mrc));
        if (from_legacy) {
            execute(pool, "UPDATE song_resources SET cloud_backend='rclone',cloud_path='/legacy/song' WHERE id=" + std::to_string(mrc));
            execute(pool, "UPDATE singer_resources SET cloud_backend='baidupcs',cloud_path='/legacy/singer',cloud_status=2 WHERE id=" + std::to_string(singer_id));
            execute(pool, "UPDATE album_resources SET cloud_backend='rclone',cloud_path='/legacy/album',cloud_status=2 WHERE id=" + std::to_string(album_id));
            execute(pool, "UPDATE song_resource_artifacts SET cloud_backend='rclone',cloud_path='/legacy/artifact' WHERE id=" + std::to_string(artifact));
        } else {
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Song, mrc}, {{"cloud_backend", "rclone"}, {"cloud_path", "/legacy/song"}});
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, singer_id}, {{"cloud_backend", "baidupcs"}, {"cloud_path", "/legacy/singer"}, {"cloud_status", 2}});
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Album, album_id}, {{"cloud_backend", "rclone"}, {"cloud_path", "/legacy/album"}, {"cloud_status", 2}});
            c::tests::retired::cloudArchive(pool, {model::ResourceOwner::SongArtifact, artifact}, {{"cloud_backend", "rclone"}, {"cloud_path", "/legacy/artifact"}});
        }
        if (from_legacy) require(support::runOnExecutor(pool->executor(), upgrade(pool), "upgrade legacy receipt schema"), "legacy expansion upgrade failed");
        require(catalog.schemaStatus().ok, "expanded schema is not ready");
        require(scalar(pool, "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN('song_resources','singer_resources','album_resources','song_resource_artifacts') AND COLUMN_NAME REGEXP '^cloud_'") == 0, "retired physical cloud columns remain");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE max_queued_transfers=10000 AND max_queued_producers=10000 AND queue_limits_version=1") == 3, "queue capacity defaults changed");
        require(scalar(pool, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN('resource_discovery_runs','resource_discovery_observations')") == 2, "discovery tables missing");
        rejected(pool, "UPDATE resource_destination_states SET max_queued_transfers=0 WHERE destination='local'");
        rejected(pool, "UPDATE resource_destination_states SET max_queued_producers=1000001 WHERE destination='local'");
        rejected(pool, "UPDATE resource_destination_states SET queue_limits_version=0 WHERE destination='local'");
        execute(pool, "INSERT INTO resource_cloud_backfills(run_id,manifest_hash,database_identity,owner_type,cloud_provider,target_revision,high_water_id,strategy) VALUES('schema-control',REPEAT('a',64),REPEAT('b',64),'song','baidu',1,0,'import_attested_skip')");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_backfills WHERE run_id='schema-control' AND control_state='active' AND control_generation=0") == 1, "legacy import control defaults changed");
        rejected(pool, "UPDATE resource_cloud_backfills SET control_state='invalid' WHERE run_id='schema-control'");
        execute(pool, "UPDATE resource_cloud_backfills SET control_state='paused',control_generation=1 WHERE run_id='schema-control'");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE dispatch_state='maintenance' AND baseline_ready=0 AND target_revision=1") == 3, "expansion enabled dispatch or baseline");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + std::to_string(mrc) + " AND status=2 AND local_path='missing/legacy.flac' AND checksum='rclone:size:123' AND EXISTS(SELECT 1 FROM resource_cloud_migration_audit a WHERE a.run_id='schema-retirement-v2026102913' AND a.owner_type='song' AND a.resource_id=song_resources.id AND JSON_UNQUOTE(JSON_EXTRACT(a.old_values,'$.cloud_path'))='/legacy/song') AND content_generation=1 AND local_skip_generation=0") == 1, "expansion changed legacy song receipt or invented a skip");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='schema-retirement-v2026102913' AND owner_type='singer' AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.cloud_path'))='/legacy/singer' AND JSON_EXTRACT(old_values,'$.cloud_status')=2") == 1 && scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='schema-retirement-v2026102913' AND owner_type='album' AND JSON_UNQUOTE(JSON_EXTRACT(old_values,'$.cloud_path'))='/legacy/album' AND JSON_EXTRACT(old_values,'$.cloud_status')=2") == 1, "expansion lost legacy cover receipt");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE EXISTS(SELECT 1 FROM resource_cloud_migration_audit a WHERE a.run_id='schema-retirement-v2026102913' AND a.owner_type='song_artifact' AND a.resource_id=song_resource_artifacts.id AND JSON_UNQUOTE(JSON_EXTRACT(a.old_values,'$.cloud_path'))='/legacy/artifact') AND content_generation=1 AND local_skip_generation=0") == 1, "expansion lost legacy artifact receipt");

        const auto insert = [&](std::string owner, std::uint64_t id, std::string column, std::string provider = "baidu") {
            execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + column + ") VALUES('" + owner + "'," + std::to_string(id) + ",'" + provider + "'," + std::to_string(id) + ")");
        };
        insert("song", mrc, "song_resource_id");
        insert("song", mrc, "song_resource_id", "gdrive");
        insert("singer", singer_id, "singer_resource_id");
        insert("album", album_id, "album_resource_id");
        insert("song_artifact", artifact, "artifact_resource_id");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 5, "four owners or dual provider identity collided");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider) VALUES('song',1,'baidu')");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id,singer_resource_id) VALUES('song'," + std::to_string(mrc) + ",'gdrive'," + std::to_string(mrc) + "," + std::to_string(singer_id) + ")");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id) VALUES('singer'," + std::to_string(mrc) + ",'gdrive'," + std::to_string(mrc) + ")");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id) VALUES('song',999999,'gdrive',999999)");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id) VALUES('song'," + std::to_string(mrc) + ",'local'," + std::to_string(mrc) + ")");
        rejected(pool, "UPDATE resource_cloud_receipts SET owner_type='album' WHERE owner_type='song'");
        rejected(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider,song_resource_id) VALUES('song'," + std::to_string(mrc) + ",'baidu'," + std::to_string(mrc) + ")");

        execute(pool, "UPDATE singer_resources SET content_generation=7,local_skip_generation=7,local_skip_baseline='fixture' WHERE id=" + std::to_string(singer_id));
        c::tests::retired::cloudArchive(pool, {model::ResourceOwner::Singer, singer_id}, {{"cloud_backend", "baidupcs"}, {"cloud_path", "/old/object"}});
        execute(pool, "UPDATE resource_destination_states SET target_revision=9,dispatch_state='paused' WHERE destination='gdrive'");
        execute(pool, "UPDATE resource_cloud_receipts SET transfer_policy='skip',task_state='skipped',verification='operator_attested',baseline_id='fixture',skip_reason='legacy_both_clouds_present' WHERE owner_type='song'");
        require(support::runOnExecutor(pool->executor(), replay(pool), "replay receipt expansion"), "receipt expansion replay failed");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE transfer_policy='skip'") == 2, "replay erased historical skip");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE destination='gdrive' AND target_revision=9 AND dispatch_state='paused'") == 1, "replay reset target state");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE content_generation=7 AND local_skip_generation=7 AND EXISTS(SELECT 1 FROM resource_cloud_migration_audit a WHERE a.run_id='schema-retirement-v2026102913' AND a.owner_type='singer' AND a.resource_id=singer_resources.id AND JSON_UNQUOTE(JSON_EXTRACT(a.old_values,'$.cloud_path'))='/old/object')") == 1, "replay reset resource or legacy columns");

        execute(pool, "DROP TRIGGER cloud_receipt_owner_update");
        const auto missing = catalog.schemaStatus();
        require(!missing.ok && std::find(missing.missing_constraints.begin(), missing.missing_constraints.end(), "cloud_receipt_owner_update") != missing.missing_constraints.end(), "schema status ignored missing identity trigger");
        require(support::runOnExecutor(pool->executor(), replay(pool), "restore receipt trigger"), "trigger restoration failed");
        require(catalog.schemaStatus().ok, "schema status not restored");
        execute(pool, "INSERT INTO resource_cloud_migration_audit(run_id,owner_type,resource_id,source_identity,content_generation,old_values,disposition) VALUES('fixture','song'," + std::to_string(mrc) + ",REPEAT('a',64),1,JSON_OBJECT('cloud_backend','rclone'),'imported')");
        execute(pool, "DELETE FROM song_resources WHERE id=" + std::to_string(mrc));
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type IN ('song','song_artifact')") == 0, "source deletion left song or artifact receipts");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='fixture'") == 1, "resource deletion erased migration evidence");
        execute(pool, "DELETE FROM singer_source_refs WHERE source_singer_id='receipt-schema-singer'");
        execute(pool, "DELETE FROM album_source_refs WHERE source_album_id='receipt-schema-album'");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == 0, "parent source cascade left orphan receipts");
        pool->cancel();
        std::cout << "PASS: isolated receipt schema, four owners, dual providers, trigger/FK rejection, maintenance gates, replay and cascades\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
