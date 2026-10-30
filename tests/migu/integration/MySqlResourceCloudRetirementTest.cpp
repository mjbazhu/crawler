#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace support = c::tests::migu::support;
namespace mysql = test::mysql;
namespace model = test::model;
namespace loader = c::infrastructure::music::persistence::sql;
namespace {
constexpr const char *migration = "sql/mysql/migrations/V2026102913__retire_resource_cloud_columns.sql";                           // 本轮收缩文件
constexpr const char *archive_run = "schema-retirement-v2026102913";                                                               // 不可变旧云证据身份
constexpr std::array<const char *, 4> tables {"song_resources", "singer_resources", "album_resources", "song_resource_artifacts"}; // 四类文件

// 正式runner处理全部版本和checksum，测试不直接伪造迁移ledger。
boost::asio::awaitable<bool> upgrade(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    co_return co_await mysql::MySqlMigrationRunner::coRun(connection, "sql/mysql/migrations");
}
// 冻结的44份文件真实建立V2912；目录和数据库均须本次新建。
std::shared_ptr<mysql::MySqlConnectionPool> oldPool(mysql::MySqlConfig config)
{
    boost::asio::io_context context;
    test::require(support::runInIoContext(context, support::createCoverTestDatabase(config), "create retirement database"), "retirement database creation failed");
    const auto directory = std::filesystem::temp_directory_path() / (config.database + "_v2912");
    test::require(std::filesystem::create_directory(directory), "retirement migration directory must be new");
    std::size_t copied = 0;
    for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations")) {
        if (entry.path().extension() != ".sql" || entry.path().filename().string() >= "V2026102913") continue;
        std::filesystem::copy_file(entry.path(), directory / entry.path().filename());
        ++copied;
    }
    test::require(copied == 44, "retirement frozen migration count changed");
    config.migrations_path = directory.string();
    config.initialize_schema = true;
    auto pool = std::make_shared<mysql::MySqlConnectionPool>(std::move(config));
    test::require(support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "initialize V2912 retirement fixture"), "V2912 initialization failed");
    return pool;
}
// 仅固定fixtureSQL设置真实旧列；弱摘要、本地路径及NULL/空字符串均有代表。
void fixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using test::execute;
    execute(pool, "INSERT INTO songs(title,normalized_title) VALUES('retirement-song','retirement-song')");
    execute(pool, "INSERT INTO song_source_refs(song_db_id,source,source_song_id,source_content_id) SELECT id,'migu','retirement-song','retirement-content' FROM songs");
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type,status,local_path,size_bytes,checksum,cloud_backend,cloud_path,cloud_uploaded_at) SELECT id,'audio','PQ',2,'retained/song.flac',3,'rclone:size:3','baidupcs','/old/song.flac',11 FROM song_source_refs");
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,status,local_path,size_bytes,checksum) SELECT id,'mrc',2,'retained/song.mrc',3,REPEAT('a',64) FROM song_source_refs");
    execute(pool, "INSERT INTO singers(name,normalized_name) VALUES('retirement-singer','retirement-singer')");
    execute(pool, "INSERT INTO singer_source_refs(singer_db_id,source,source_singer_id) SELECT id,'migu','retirement-singer' FROM singers");
    execute(pool, "INSERT INTO singer_resources(singer_source_ref_id,status,remote_url,local_path,size_bytes,checksum,cloud_backend,cloud_path,cloud_size_bytes,cloud_checksum,cloud_status,cloud_uploaded_at,cloud_claim_generation,cloud_last_error) SELECT id,2,'https://example.test/singer','retained/singer.png',3,REPEAT('a',64),'baidupcs','',3,REPEAT('a',64),2,22,7,'' FROM singer_source_refs");
    execute(pool, "INSERT INTO albums(title,normalized_title,identity_hash) VALUES('retirement-album','retirement-album',SHA2('migu|retirement-album',256))");
    execute(pool, "INSERT INTO album_source_refs(album_db_id,source,source_album_id) SELECT id,'migu','retirement-album' FROM albums");
    execute(pool, "INSERT INTO album_resources(album_source_ref_id,status,remote_url,local_path,size_bytes,checksum,cloud_backend,cloud_path,cloud_size_bytes,cloud_checksum,cloud_status,cloud_uploaded_at) SELECT id,2,'https://example.test/album','retained/album.png',3,'md5:retained','rclone','null',3,'md5:old-cloud',2,33 FROM album_source_refs");
    execute(pool, "INSERT INTO song_resource_artifacts(source_resource_id,artifact_type,source_checksum,processor_version,status,local_path,size_bytes,checksum,cloud_backend,cloud_path) SELECT id,'word_json',REPEAT('a',64),'fixture',2,'retained/words.json',3,REPEAT('c',64),'rclone',NULL FROM song_resources WHERE resource_kind='mrc'");
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const std::string table = tables[i], owner(model::resourceOwnerName(static_cast<model::ResourceOwner>(i)));
        execute(pool, "UPDATE " + table + " SET local_skip_generation=content_generation,local_skip_target_revision=1,local_skip_baseline='retirement-skip',local_skip_reason='operator_attested'");
        const auto fk = i == 0 ? "song_resource_id" : i == 1 ? "singer_resource_id" :
                                                  i == 2     ? "album_resource_id" :
                                                               "artifact_resource_id";
        for (const auto provider : {"baidu", "gdrive"})
            execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_provider," + std::string(fk) + ",task_state,content_generation,target_revision,transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason,verification,evidence) SELECT '" + owner + "',id,'" + provider + "',id,'skipped',content_generation,1,'skip',content_generation,1,'retirement-skip','operator_attested','operator_attested',JSON_OBJECT('kept','original') FROM " + table);
    }
    execute(pool, "UPDATE resource_cloud_receipts SET task_state='succeeded',confirmed_content_generation=1,confirmed_target_revision=1,checksum=REPEAT('a',64),size_bytes=3,cloud_path='new/verified-singer',uploaded_at=444,verified_at=555,verification='sha256_verified',source_adapter='baidupcs' WHERE owner_type='singer' AND cloud_provider='baidu'");
}
// 按主键排序逐行签名，避免聚合返回顺序变化造成伪差异。
std::string rowSignature(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &table, const std::string &fields, const std::string &where = "")
{
    const auto rows = test::execute(pool, "SELECT SHA2(CAST(JSON_ARRAY(" + fields + ") AS CHAR),256) FROM " + table + where + " ORDER BY id");
    std::string signature;
    for (const auto row : rows.rows()) signature += mysql::MySqlValueReader::stringAt(row, 0);
    return signature;
}
// 原表中除旧云列外的所有字段一并签名，防只检查路径而漏掉其他业务值。
std::string tableSignature(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &table, const std::string &exclude = "")
{
    const auto columns = test::execute(pool, "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='" + table + "'" + exclude + " ORDER BY ORDINAL_POSITION");
    std::string fields;
    for (const auto row : columns.rows()) {
        if (!fields.empty()) fields += ',';
        fields += '`' + mysql::MySqlValueReader::stringAt(row, 0) + '`';
    }
    return rowSignature(pool, table, fields);
}
// 升级前旧云原值与原清单摘要，按owner/id精确比对。
struct Before {
    model::ResourceIdentity identity; // 文件行身份
    std::string cloud_hash;           // MySQL原始JSON规范摘要
    std::string manifest_hash;        // V2912旧snapshot摘要
    std::string source_identity;      // 来源身份，不依赖旧云字段
    std::uint64_t generation = 0;     // 原内容代次
};
// 以旧物理列计算升级前快照；不能用新回执推测历史值。
std::vector<Before> captureBefore(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    std::vector<Before> before;
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const std::string table = tables[i];
        const auto columns = test::execute(pool, "SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='" + table + "' AND COLUMN_NAME REGEXP '^cloud_' ORDER BY ORDINAL_POSITION");
        std::string object;
        for (const auto row : columns.rows()) {
            const auto key = mysql::MySqlValueReader::stringAt(row, 0);
            if (!object.empty()) object += ',';
            object += "'" + key + "',`" + key + '`';
        }
        const auto uploaded = i == 3 ? "NULL" : "cloud_uploaded_at";
        const auto size = i == 1 || i == 2 ? "cloud_size_bytes" : "NULL";
        const auto checksum = i == 1 || i == 2 ? "cloud_checksum" : "NULL";
        const auto status = i == 1 || i == 2 ? "cloud_status" : "NULL";
        const auto legacy = "JSON_OBJECT('local_path',COALESCE(local_path,''),'size_bytes',size_bytes,'checksum',COALESCE(checksum,''),'status',status,'cloud_backend',COALESCE(cloud_backend,''),'cloud_path',COALESCE(cloud_path,''),'cloud_uploaded_at'," + std::string(uploaded) + ",'cloud_size_bytes'," + size + ",'cloud_checksum'," + checksum + ",'cloud_status'," + status + ")";
        const auto rows = test::execute(pool, "SELECT id,CAST(JSON_OBJECT(" + object + ") AS CHAR),SHA2(CAST(JSON_OBJECT(" + object + ") AS CHAR),256),SHA2(CAST(" + legacy + " AS CHAR),256) FROM " + table + " ORDER BY id");
        for (const auto row : rows.rows()) {
            const model::ResourceIdentity identity {static_cast<model::ResourceOwner>(i), mysql::MySqlValueReader::uint64At(row, 0)};
            const auto source = mysql::MySqlLegacyResourceImport(pool).preview(identity.owner, identity.resource_id - 1, identity.resource_id, 1);
            test::require(source.isSuccess() && source.data()->rows.size() == 1, "original source identity missing");
            Before saved;
            saved.identity = identity;
            saved.cloud_hash = mysql::MySqlValueReader::stringAt(row, 2);
            saved.manifest_hash = mysql::MySqlValueReader::stringAt(row, 3);
            saved.source_identity = source.data()->rows.front().source_identity;
            saved.generation = source.data()->rows.front().generation;
            before.push_back(std::move(saved));
        }
    }
    return before;
}
// 归档/字段未动之前，在途旧云认领及意图必须先拒绝。
void refusedClaims(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    for (const auto *setting : {"cloud_status=1", "cloud_status=4", "cloud_status=2,cloud_attempt=JSON_OBJECT('path','pending')", "cloud_attempt=NULL,cloud_running_batch_id='pending'"}) {
        test::execute(pool, "UPDATE singer_resources SET " + std::string(setting));
        test::require(!support::runOnExecutor(pool->executor(), upgrade(pool), "reject legacy cloud claim"), "retirement accepted old cloud claim/attempt");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN('song_resources','singer_resources','album_resources','song_resource_artifacts') AND COLUMN_NAME REGEXP '^cloud_'") == 31, "rejected retirement deleted columns");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='schema-retirement-v2026102913'") == 0, "rejected retirement sealed archive");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102913") == 0 && test::scalar(pool, "SELECT COUNT(*) FROM crawler_schema_versions WHERE component='crawler_cloud_archive'") == 0, "rejected retirement recorded success");
        test::execute(pool, "UPDATE singer_resources SET cloud_status=2,cloud_attempt=NULL,cloud_running_batch_id=NULL");
    }
}
// 执行真实分句直到drop调用前，模拟归档已提交而只完成一张表的DDL。
boost::asio::awaitable<bool> interrupted(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    bool stopped = false;
    for (const auto &statement : loader::SqlFileLoader::splitMigrationStatements(loader::SqlFileLoader::loadOrThrow(migration))) {
        if (statement.find("CALL crawler_drop_cloud_v2913()") != std::string::npos) {
            stopped = true;
            break;
        }
        co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    }
    test::require(stopped, "retirement DDL interruption boundary missing");
    co_await connection->async_execute("ALTER TABLE song_resources DROP COLUMN cloud_backend,DROP COLUMN cloud_path,DROP COLUMN cloud_uploaded_at", result, boost::asio::use_awaitable);
    co_return true;
}
// 收缩后四类资源均走正式页事务，覆盖JSON_TABLE与显式资源/来源锁的组合。
void lockedImports(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    mysql::MySqlLegacyResourceImport importer(pool);
    for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive})
        test::require(importer.configureTarget(destination, 1, std::string(64, 'a')).isSuccess(), "retirement lock target setup failed");
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto owner = static_cast<model::ResourceOwner>(i);
        const auto page = importer.preview(owner, 0, 0, 500);
        test::require(page.isSuccess() && !page.data()->rows.empty(), "retirement lock preview missing");
        model::LegacyResourceImportPlan plan;
        plan.run_id = "retirement-lock-" + std::string(model::resourceOwnerName(owner));
        plan.manifest_hash = std::string(64, 'a');
        plan.operator_excluded_all = true;
        const auto imported = importer.importPage(plan, *page.data());
        test::require(imported.isSuccess() && imported.data()->conflicts == 0 && imported.data()->excluded_resources == page.data()->rows.size() && imported.data()->new_receipts == 0, "retired snapshot locking page failed");
    }
}
} // namespace

// 真正升级冻结V2912、故障拒绝和逐表DDL断点；不启动服务或访问内容端点。
int main()
{
    try {
        const auto config = support::makeMySqlConfigFromEnv();
        test::require(config.database.starts_with("crawler_ownership_test_retirement_"), "refusing non-isolated retirement database");
        auto pool = oldPool(config);
        test::require(test::scalar(pool, "SELECT (SELECT COUNT(*) FROM songs)+(SELECT COUNT(*) FROM singers)+(SELECT COUNT(*) FROM albums)") == 0, "retirement test requires empty business tables");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM schema_migrations") == 44 && test::scalar(pool, "SELECT MAX(version) FROM schema_migrations") == 2026102912, "old retirement fixture was not V2912");
        fixtures(pool);
        const auto default_mrc = test::scalar(pool, "SELECT id FROM song_resources WHERE resource_kind='mrc'");
        const auto before = captureBefore(pool);
        refusedClaims(pool);
        std::map<std::string, std::string> locals;
        for (const auto *table : tables) locals[table] = tableSignature(pool, table, " AND COLUMN_NAME NOT REGEXP '^cloud_'");
        const auto receipts = tableSignature(pool, "resource_cloud_receipts", " AND COLUMN_NAME<>'evidence'");
        test::require(support::runOnExecutor(pool->executor(), interrupted(pool), "simulate table DDL commit"), "retirement interruption fixture failed");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM crawler_schema_versions WHERE component='crawler_cloud_archive' AND schema_version=2026102913") == 1 && test::scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102913") == 0, "DDL interruption did not leave sealed/unrecorded boundary");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN('song_resources','singer_resources','album_resources','song_resource_artifacts') AND COLUMN_NAME REGEXP '^cloud_'") == 28, "DDL interruption did not drop exactly one table's cloud columns");
        test::require(support::runOnExecutor(pool->executor(), upgrade(pool), "resume retirement migration"), "retirement could not resume partial DDL");
        test::require(mysql::MySqlMusicCatalogQueryRepository(pool).schemaStatus().ok, "retired schema was not ready");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN('song_resources','singer_resources','album_resources','song_resource_artifacts') AND COLUMN_NAME REGEXP '^cloud_'") == 0 && test::scalar(pool, "SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND INDEX_NAME IN('idx_singer_resources_cloud_queue','idx_album_resources_cloud_queue')") == 0, "old columns/indexes survived retirement");
        for (const auto *table : tables) test::require(locals.at(table) == tableSignature(pool, table, " AND COLUMN_NAME NOT REGEXP '^cloud_'"), "retirement changed non-cloud resource fields");
        for (const auto &row : before) {
            const std::string owner(model::resourceOwnerName(row.identity.owner));
            const auto current = mysql::MySqlLegacyResourceImport(pool).preview(row.identity.owner, row.identity.resource_id - 1, row.identity.resource_id, 1);
            test::require(current.isSuccess() && current.data()->rows.size() == 1 && current.data()->rows.front().values_digest == row.manifest_hash && current.data()->rows.front().source_identity == row.source_identity && current.data()->rows.front().generation == row.generation, "retirement changed original manifest digest");
            const auto archived = test::execute(pool, "SELECT SHA2(CAST(old_values AS CHAR),256),source_identity,content_generation FROM resource_cloud_migration_audit WHERE run_id='" + std::string(archive_run) + "' AND owner_type='" + owner + "' AND resource_id=" + std::to_string(row.identity.resource_id));
            if (row.identity.owner == model::ResourceOwner::Song && row.identity.resource_id == default_mrc)
                test::require(archived.rows().empty(), "default old cloud fields created redundant archive");
            else
                test::require(archived.rows().size() == 1 && mysql::MySqlValueReader::stringAt(archived.rows().front(), 0) == row.cloud_hash && mysql::MySqlValueReader::stringAt(archived.rows().front(), 1) == row.source_identity && mysql::MySqlValueReader::uint64At(archived.rows().front(), 2) == row.generation, "archive changed exact old cloud JSON");
        }
        const auto after = tableSignature(pool, "resource_cloud_receipts", " AND COLUMN_NAME<>'evidence'");
        test::require(receipts == after && test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE JSON_UNQUOTE(JSON_EXTRACT(evidence,'$.kept'))='original'") == 10, "retirement downgraded current receipts or unrelated evidence");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='singer' AND cloud_provider='baidu' AND uploaded_at=444 AND verified_at=555 AND verification='sha256_verified' AND JSON_EXTRACT(evidence,'$.retired_legacy.cloud_uploaded_at')=22") == 1, "historical uploaded time replaced actual confirmation");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_migration_audit WHERE run_id='schema-retirement-v2026102913' AND disposition='archived'") == 4, "retirement archive scope changed");
        test::require(test::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE cloud_provider='gdrive' AND JSON_CONTAINS_PATH(COALESCE(evidence,JSON_OBJECT()),'one','$.retired_legacy')") == 0, "migration guessed an rclone provider");
        lockedImports(pool);
        test::require(after == tableSignature(pool, "resource_cloud_receipts", " AND COLUMN_NAME<>'evidence'"), "excluded lock regression changed current receipts");
        const auto archive_state = tableSignature(pool, "resource_cloud_migration_audit");
        test::require(support::runOnExecutor(pool->executor(), upgrade(pool), "repeat retirement"), "retirement repeat failed");
        test::require(archive_state == tableSignature(pool, "resource_cloud_migration_audit"), "repeat retirement changed archive");
        std::cout << "PASS: frozen V2912 to V2913, legacy intent refusal, exact four-owner archive/local/current receipt preservation, stable historical digests and one-table DDL interruption recovery\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
