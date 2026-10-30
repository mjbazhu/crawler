#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace {
// 所有诊断为测试自有固定文字，不回显业务或认证。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
// 本测试只读写名称已限定的全新隔离库。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(text)), "migration recovery fixture");
}
// 读取非空标量，避免以默认0掩盖无结果。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    const auto result = execute(pool, std::move(text));
    require(result.rows().size() == 1, "migration recovery scalar missing");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}
// 单独借用连接运行正式runner；不自动删除或覆盖任何数据库。
boost::asio::awaitable<bool> run(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::string directory)
{
    auto connection = co_await pool->coAcquire();
    co_return co_await mysql::MySqlMigrationRunner::coRun(connection, std::move(directory));
}
// 检查已存在业务和关联行均保留，派生定义不替代数据保留断言。
void preserved(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    require(scalar(pool, "SELECT COUNT(*) FROM singers WHERE normalized_name='migration-recovery-sentinel' AND name='migration-recovery'") == 1, "migration recovery lost business row");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_related WHERE discovered_mask=1 AND claim_generation=2") == 1, "migration recovery lost related row");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_operations WHERE operation_id='migration-recovery' AND JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id'))='sentinel-parent'") == 1, "migration recovery changed source JSON");
}
// 正确的两个状态版本必须一致；未来双方一致的版本只读状态仍可兼容。
void consistent(mysql::MySqlMusicCatalogQueryRepository &catalog)
{
    const auto status = catalog.schemaStatus();
    require(status.ok && status.schema_version == 2026102913 && status.latest_applied_version == 2026102913, "migration component and ledger differ");
}
} // namespace

// 验证元数据同步、V2910DDL完成后的记账恢复及不兼容定义失败关闭。
int main()
{
    try {
        const auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_migration_recovery_"), "refusing non-isolated migration recovery database");
        auto pool = support::makeCoverTestPool();
        require(scalar(pool, "SELECT COUNT(*) FROM singers") == 0, "requires fresh migration recovery database");
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        consistent(catalog);
        execute(pool, "INSERT INTO singers(name,normalized_name) VALUES('migration-recovery','migration-recovery-sentinel')");
        execute(pool, "INSERT INTO resource_transfer_operations(operation_id,request_hash,request_snapshot,action,logical_resource_count,task_count,created_at,updated_at) VALUES('migration-recovery',REPEAT('a',64),JSON_OBJECT('parent_operation_id','sentinel-parent'),'process',1,1,0,0)");
        execute(pool, "INSERT INTO resource_transfer_tasks(operation_id,owner_type,resource_id,destination,content_generation,source_revision,source_identity,target_revision,target_fingerprint,timeout_ms,retries,max_bytes,created_at,updated_at) VALUES('migration-recovery','song',1,'local',1,1,REPEAT('a',64),1,REPEAT('b',64),1000,0,1,0,0)");
        execute(pool, "INSERT INTO resource_transfer_related(parent_task_id,discovered_mask,claim_generation,created_at,updated_at) SELECT task_id,1,2,0,0 FROM resource_transfer_tasks WHERE operation_id='migration-recovery'");
        const auto applied = scalar(pool, "SELECT COUNT(*) FROM schema_migrations");

        execute(pool, "UPDATE crawler_schema_versions SET schema_version=2026102902 WHERE component='crawler_mysql'");
        require(!catalog.schemaStatus().ok, "stale component marker was reported ready");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "synchronize component marker"), "no-pending marker repair failed");
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations") == applied, "marker repair changed migration history");
        consistent(catalog);
        preserved(pool);

        execute(pool, "UPDATE crawler_schema_versions SET updated_at=123 WHERE component='crawler_mysql'");
        const auto first_applied = scalar(pool, "SELECT applied_at FROM crawler_schema_versions WHERE component='crawler_mysql'");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "idempotent component synchronization"), "unchanged component rerun failed");
        require(scalar(pool, "SELECT updated_at FROM crawler_schema_versions WHERE component='crawler_mysql'") == 123 && scalar(pool, "SELECT applied_at FROM crawler_schema_versions WHERE component='crawler_mysql'") == first_applied, "unchanged component rerun changed timestamps");
        execute(pool, "DELETE FROM crawler_schema_versions WHERE component='crawler_mysql'");
        require(!catalog.schemaStatus().ok, "missing component marker was reported ready");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "restore missing component marker"), "missing component marker repair failed");
        consistent(catalog);

        execute(pool, "DELETE FROM schema_migrations WHERE version=2026102910");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reconcile completed V2910 DDL"), "completed DDL was not reconciled");
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations") == applied && scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102910 AND checksum='840cc00d07e8a0881b63c2411a8090ec'") == 1, "reconciled ledger incorrect");
        consistent(catalog);
        preserved(pool);

        execute(pool, "DELETE FROM schema_migrations WHERE version=2026102910");
        execute(pool, "ALTER TABLE resource_transfer_operations MODIFY COLUMN parent_operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin GENERATED ALWAYS AS (NULLIF(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.wrong_parent')),'')) STORED");
        require(!support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reject wrong generated expression"), "wrong generated definition was recorded");
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102910") == 0, "recovery wrote ledger for incompatible DDL");
        preserved(pool);
        execute(pool, "ALTER TABLE resource_transfer_operations MODIFY COLUMN parent_operation_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin GENERATED ALWAYS AS (NULLIF(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id')),'')) STORED");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reconcile restored definition"), "corrected definition could not recover");

        execute(pool, "DELETE FROM schema_migrations WHERE version=2026102910");
        execute(pool, "ALTER TABLE resource_transfer_operations DROP INDEX idx_transfer_parent_operation,DROP COLUMN parent_task_id");
        require(!support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reject partial generated DDL"), "partial DDL was recorded");
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102910") == 0, "partial recovery wrote migration ledger");
        preserved(pool);
        execute(pool, "ALTER TABLE resource_transfer_operations ADD COLUMN parent_task_id BIGINT UNSIGNED GENERATED ALWAYS AS (CAST(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_task_id')) AS UNSIGNED)) STORED,ADD KEY idx_transfer_parent_operation(parent_operation_id,operation_id)");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reconcile completed restored DDL"), "restored complete DDL did not recover");

        // 只修改独立临时副本，冻结的仓库迁移文件保持原字节。
        execute(pool, "DELETE FROM schema_migrations WHERE version=2026102910");
        const auto altered = std::filesystem::temp_directory_path() / (config.database + "_checksum_fixture");
        require(std::filesystem::create_directory(altered), "requires new checksum fixture directory");
        for (const auto &entry : std::filesystem::directory_iterator(config.migrations_path))
            if (entry.path().extension() == ".sql") std::filesystem::copy_file(entry.path(), altered / entry.path().filename());
        {
            std::ofstream changed(altered / "V2026102910__resource_related_discovery.sql", std::ios::app);
            changed << "\n-- isolated checksum mismatch fixture\n";
            require(changed.good(), "checksum fixture write failed");
        }
        require(!support::runOnExecutor(pool->executor(), run(pool, altered.string()), "reject changed frozen recovery script"), "changed frozen recovery checksum accepted");
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations WHERE version=2026102910") == 0, "changed recovery script wrote ledger");
        preserved(pool);
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "restore frozen recovery ledger"), "frozen recovery script failed");

        execute(pool, "UPDATE crawler_schema_versions SET schema_version=9999999999 WHERE component='crawler_mysql'");
        require(!support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reject future component downgrade"), "future component marker downgraded");
        require(scalar(pool, "SELECT schema_version FROM crawler_schema_versions WHERE component='crawler_mysql'") == 9999999999ULL, "future component marker changed");
        execute(pool, "INSERT INTO schema_migrations(version,name,checksum,applied_at,execution_ms) VALUES(9999999999,'future_fixture',REPEAT('f',32),0,0)");
        require(catalog.schemaStatus().ok, "matching future component and ledger lost read-only compatibility");
        require(!support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "reject unknown future ledger"), "unknown future migration was downgraded");
        require(scalar(pool, "SELECT schema_version FROM crawler_schema_versions WHERE component='crawler_mysql'") == 9999999999ULL, "future ledger guard changed marker");
        execute(pool, "DELETE FROM schema_migrations WHERE version=9999999999");
        execute(pool, "UPDATE crawler_schema_versions SET schema_version=2026102902 WHERE component='crawler_mysql'");
        require(support::runOnExecutor(pool->executor(), run(pool, config.migrations_path), "restore final component consistency"), "final marker recovery failed");
        consistent(catalog);
        preserved(pool);
        require(scalar(pool, "SELECT COUNT(*) FROM schema_migrations") == applied, "recovery changed migration cardinality");
        std::cout << "PASS: component consistency, frozen V2910 recovery, incompatible/partial rejection, data retention and future guards\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: migration recovery: " << error.what() << '\n';
        return 1;
    }
}
