#include "migu/support/ResourceServiceRehearsalSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"

#include <fstream>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace service = c::tests::service_rehearsal;
namespace mysql = service::mysql;
namespace model = service::model;
using Json = service::Json;

namespace {
// 只接受父进程的本机HTTP fixture，禁止把测试探针当作任意来源登记工具。
bool loopback(const std::string &url)
{
    const std::string prefix = "http://127.0.0.1:";
    if (!url.starts_with(prefix)) return false;
    const auto suffix = url.substr(prefix.size());
    return !suffix.empty() && suffix.size() <= 5 && suffix.find_first_not_of("0123456789") == std::string::npos && std::stoul(suffix) > 0 && std::stoul(suffix) <= 65535;
}

// 实际本地目录身份使用正式verifier；双云仅在隔离库保持暂停，绝不发起云端请求。
void setup(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::filesystem::path &root, const std::string &url)
{
    test::require(loopback(url) && std::filesystem::is_directory(root), "loopback source and local root required");
    const model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::seconds(10), {}};
    const auto observed = c::infrastructure::app::verifyLocalResourceTarget(root, context);
    test::require(observed.profile.available, "actual local target unavailable");
    test::execute(pool, "UPDATE resource_destination_states SET dispatch_state='paused',baseline_ready=0,target_fingerprint=''");
    test::execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64),target_fingerprint='" + observed.profile.fingerprint + "' WHERE destination='local'");
    Json ids = Json::array();
    for (int index = 1; index <= 2; ++index) {
        const auto id = test::singer(pool, index);
        test::execute(pool, "UPDATE singer_resources SET remote_url='" + url + "/" + std::to_string(index) + ".png' WHERE id=" + std::to_string(id));
        ids.push_back(id);
    }
    // 仅拦正式完成回执，不拦claim、reference或attempt登记；不写生产migration。
    test::execute(pool, "CREATE TRIGGER service_rehearsal_complete BEFORE UPDATE ON singer_resources FOR EACH ROW BEGIN DECLARE acquired INT DEFAULT 0; IF OLD.status=1 AND NEW.status=2 AND OLD.running_batch_id LIKE 'rt:%' AND NEW.local_path IS NOT NULL AND NEW.local_path<>'' AND NEW.local_content_generation>0 THEN SET acquired=GET_LOCK(CONCAT('svc_',LEFT(SHA2(CONCAT(DATABASE(),':',OLD.id),256),48)),120); IF acquired<>1 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='service_rehearsal_barrier_timeout'; END IF; DO RELEASE_LOCK(CONCAT('svc_',LEFT(SHA2(CONCAT(DATABASE(),':',OLD.id),256),48))); END IF; END");
    service::output({{"seeded", 2}, {"resource_ids", ids}, {"local_fingerprint", observed.profile.fingerprint}, {"cloud_requests", 0}, {"fixture_baseline", true}});
}

// 无锁读取已提交快照，完成事务在测试锁等待时也能检查attempt及原认领。
void snapshot(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    const auto rows = test::execute(pool, "SELECT t.task_id,t.operation_id,t.resource_id,t.state,t.claim_generation,t.native_claim_generation,t.claimed_at,COALESCE(t.attempt_path,''),t.attempt_checksum,t.attempt_size_bytes,o.action,t.result_code FROM resource_transfer_tasks t JOIN resource_transfer_operations o ON o.operation_id=t.operation_id ORDER BY t.task_id");
    Json tasks = Json::array();
    for (const auto row : rows.rows()) {
        tasks.push_back({{"task_id", mysql::MySqlValueReader::uint64At(row, 0)}, {"operation_id", mysql::MySqlValueReader::stringAt(row, 1)}, {"resource_id", mysql::MySqlValueReader::uint64At(row, 2)}, {"state", mysql::MySqlValueReader::stringAt(row, 3)}, {"claim_generation", mysql::MySqlValueReader::uint64At(row, 4)}, {"native_claim_generation", mysql::MySqlValueReader::uint64At(row, 5)}, {"claimed_at", mysql::MySqlValueReader::uint64At(row, 6)}, {"attempt_path", mysql::MySqlValueReader::stringAt(row, 7)}, {"attempt_checksum", mysql::MySqlValueReader::stringAt(row, 8)}, {"attempt_size_bytes", mysql::MySqlValueReader::uint64At(row, 9)}, {"action", mysql::MySqlValueReader::stringAt(row, 10)}, {"result_code", mysql::MySqlValueReader::stringAt(row, 11)}});
    }
    service::output({{"tasks", tasks}, {"waiting_receipts", test::scalar(pool, "SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE DB=DATABASE() AND STATE='User lock'")}, {"slots", test::scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots")}, {"completed_resources", test::scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE status=2")}, {"producer_tasks", test::scalar(pool, "SELECT COUNT(*) FROM resource_producer_tasks")}});
}

// 恢复前的真实worker认领在恢复及显式修复后都不能确认；探针自身不认领或发布。
void stale(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::filesystem::path &path)
{
    test::require(std::filesystem::file_size(path) < 16384, "unbounded claim record");
    std::ifstream input(path);
    const auto records = Json::parse(input);
    test::require(records.is_array() && records.size() == 2, "two original claims required");
    mysql::MySqlResourceTransferJobs jobs(pool);
    for (const auto &record : records) {
        const auto operation = test::get(jobs.find(record.at("operation_id")));
        test::require(operation.tasks.size() == 1, "original operation scope changed");
        auto job = operation.tasks.front();
        test::require(job.task_id == record.at("task_id").get<std::uint64_t>(), "original task mismatch");
        job.claim_generation = record.at("claim_generation");
        job.native_claim_generation = record.at("native_claim_generation");
        model::ResourceTransferReceipt receipt;
        receipt.path = record.at("attempt_path");
        receipt.checksum = record.at("attempt_checksum");
        receipt.size_bytes = record.at("attempt_size_bytes");
        receipt.adapter = "filesystem";
        receipt.verification_method = "local_sha256";
        receipt.verification = model::ResourceVerification::Sha256Verified;
        test::require(!test::get(jobs.complete(job, receipt)), "stale service claim completed");
    }
    service::output({{"stale_claims_rejected", 2}, {"stale_commits", 0}});
}
} // namespace

// 仅管理独立库fixture与观测；正式受理、下载、发布和修复必须由demo执行。
int main(int argc, char **argv)
{
    try {
        test::require(argc >= 2, "explicit service probe mode required");
        const std::string mode = argv[1];
        auto config = c::tests::migu::support::makeMySqlConfigFromEnv();
        test::require(config.host == "127.0.0.1" && config.database.starts_with("crawler_ownership_test_service_") && c::tests::migu::support::safeCoverDatabaseName(config.database), "isolated loopback service database required");
        if (mode == "barrier" && argc == 3) {
            const auto resource = std::stoull(argv[2]);
            test::require(resource > 0, "invalid barrier resource");
            boost::asio::io_context context;
            return c::tests::migu::support::runInIoContext(context, service::barrier(config, resource), "service barrier", std::chrono::seconds(180)) ? 0 : 1;
        }
        if (mode == "setup") {
            test::require(argc == 4, "setup root and source required");
            boost::asio::io_context context;
            test::require(c::tests::migu::support::runInIoContext(context, service::createDatabase(config), "create fresh service database"), "create database failed");
        }
        config.initialize_schema = mode == "setup";
        auto pool = std::make_shared<mysql::MySqlConnectionPool>(config);
        if (mode == "setup") {
            test::require(c::tests::migu::support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "service schema"), "schema failed");
            setup(pool, std::filesystem::u8path(argv[2]), argv[3]);
        } else if (mode == "snapshot" && argc == 2) {
            snapshot(pool);
        } else if (mode == "recover" && argc == 2) {
            mysql::MySqlResourceTransferJobs jobs(pool);
            const auto recovered = test::get(jobs.recover(model::ResourceDestination::Local, 1));
            test::require(recovered == 2, "expected two expired service claims");
            service::output({{"recovered", recovered}});
        } else if (mode == "stale" && argc == 3) {
            stale(pool, std::filesystem::u8path(argv[2]));
        } else {
            throw std::runtime_error("invalid service probe mode");
        }
        return 0;
    } catch (const std::exception &) {
        std::cerr << "FAIL: isolated service rehearsal assertion or dependency failed\n";
        return 1;
    }
}
