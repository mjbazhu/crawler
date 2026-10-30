#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "migu/support/ResourceArchiveMySqlSupport.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::archive;
namespace mysql = test::mysql;
namespace model = test::model;
using Json = nlohmann::json;
using D = model::ResourceDestination;

namespace {
// 固定三字节隔离内容，不读取音乐源或生产文件。
constexpr auto digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
// 原子进程结束前刷新机器可读记录；认证及来源URL不进入输出。
void output(const Json &value)
{
    std::cout << value.dump() << std::endl;
}
// 对固定fixture内容构造本地确认；Python另行重新计算文件SHA-256。
model::ResourceTransferReceipt receipt(std::string path)
{
    model::ResourceTransferReceipt value;
    value.path = std::move(path);
    value.checksum = digest;
    value.size_bytes = 3;
    value.adapter = "filesystem";
    value.verification = model::ResourceVerification::Sha256Verified;
    value.verification_method = "local_sha256";
    return value;
}
// 只在全空的新隔离库登记两个独立资源；合成目标不构成发布基线。
void setup(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    require(scalar(pool, "SELECT COUNT(*) FROM singers") == 0 && scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == 0, "requires empty crash fixture");
    activate(pool);
    mysql::MySqlResourceTransferJobs jobs(pool);
    for (int index = 1; index <= 2; ++index) {
        get(jobs.enqueue(request(model::ResourceOwner::Singer, singer(pool, index), "crash-" + std::to_string(index)), profiles()));
    }
    output({{"seeded", 2}, {"synthetic_targets", true}, {"cloud_requests", 0}});
}
// 真实认领并保存独占对象，在确认前等待外部父进程强制终止。
void hold(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::filesystem::path &root, std::string role)
{
    using namespace test;
    require(role == "one" || role == "two", "invalid crash role");
    mysql::MySqlResourceTransferJobs jobs(pool);
    output({{"event", "claiming"}});
    auto claimed = get(jobs.claim(D::Local, std::string(64, 'a'), "filesystem"));
    // 两进程可同时看到同一候选；输掉该次竞争后按正常worker语义轮询下一项。
    for (int retry = 0; !claimed && retry < 40; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        claimed = get(jobs.claim(D::Local, std::string(64, 'a'), "filesystem"));
    }
    require(claimed.has_value(), "crash worker claim missing");
    output({{"event", "claimed"}, {"task_id", claimed->task_id}});
    const auto &job = *claimed;
    const auto path = root / (std::to_string(job.task_id) + ".object");
    require(!std::filesystem::exists(path), "refusing existing fixture object");
    require(get(jobs.beginAttempt(job, {path.generic_string(), digest, 3})), "crash attempt not persisted");
    output({{"event", "attempt_persisted"}, {"task_id", job.task_id}});
    {
        std::ofstream object(path, std::ios::binary);
        object << "abc";
        object.close();
        require(!object.fail(), "fixture publication failed");
    }
    const Json record = {{"task_id", job.task_id}, {"operation_id", job.operation_id}, {"resource_id", job.resource.resource_id}, {"claim_generation", job.claim_generation}, {"native_claim_generation", job.native_claim_generation}, {"path", path.generic_string()}};
    {
        const auto manifest = root / (role + ".json");
        require(!std::filesystem::exists(manifest), "refusing existing worker manifest");
        const auto temporary = root / (role + ".json.tmp");
        require(!std::filesystem::exists(temporary), "refusing existing temporary manifest");
        std::ofstream stream(temporary);
        stream << record.dump();
        stream.close();
        require(!stream.fail(), "worker manifest failed");
        std::filesystem::rename(temporary, manifest);
    }
    output(record);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    throw std::runtime_error("parent failed to terminate worker within budget");
}
// 使用真实过期时间恢复；已发布但未确认的对象必须保留独占槽。
void recover(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    using namespace test;
    mysql::MySqlResourceTransferJobs jobs(pool);
    const auto recovered = get(jobs.recover(D::Local, 1));
    require(recovered == 2, "expected two expired crash claims");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks WHERE state='uncertain' AND attempt_path<>''") == 2, "uncertain objects were discarded");
    require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == 2, "uncertain slots were released");
    require(!get(jobs.claim(D::Local, std::string(64, 'a'), "filesystem")).has_value(), "uncertain work was silently retransmitted");
    output({{"recovered", recovered}, {"uncertain_slots_preserved", 2}});
}
// 显式核对保留对象后修复；恢复前的认领在修复前后均不能确认。
void repair(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::filesystem::path &root, std::string role)
{
    using namespace test;
    require(role == "one" || role == "two", "invalid repair role");
    const auto manifest = root / (role + ".json");
    require(std::filesystem::file_size(manifest) < 4096, "worker manifest unbounded");
    std::ifstream input(manifest);
    const auto record = Json::parse(input);
    std::ifstream object(std::filesystem::u8path(record.at("path").get<std::string>()), std::ios::binary);
    const std::string content {std::istreambuf_iterator<char>(object), {}};
    require(content == "abc", "preserved object mismatch");
    mysql::MySqlResourceTransferJobs jobs(pool);
    const auto operation = get(jobs.find(record.at("operation_id")));
    require(operation.tasks.size() == 1, "unexpected original operation scope");
    auto stale = operation.tasks.front();
    require(stale.task_id == record.at("task_id").get<std::uint64_t>(), "original task mismatch");
    stale.claim_generation = record.at("claim_generation");
    stale.native_claim_generation = record.at("native_claim_generation");
    const auto evidence = receipt(record.at("path"));
    require(!get(jobs.complete(stale, evidence)), "stale worker committed after recovery");
    auto request = test::request(model::ResourceOwner::Singer, record.at("resource_id"), "repair-" + role);
    request.action = model::ResourceTransferAction::Repair;
    get(jobs.enqueue(request, profiles()));
    const auto job = get(jobs.claim(D::Local, std::string(64, 'a'), "filesystem"));
    require(job.has_value() && job->resource.resource_id == stale.resource.resource_id && job->attempt_path == evidence.path, "repair did not retain exact attempt");
    require(get(jobs.complete(*job, evidence)), "explicit repair did not complete");
    require(!get(jobs.complete(stale, evidence)), "stale worker committed after repair");
    output({{"repaired", 1}, {"stale_commits", 0}, {"remaining_slots", scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots")}});
}
} // namespace

// 本程序只是多进程探针；完整强杀和文件核验由Python父进程执行。
int main(int argc, char **argv)
{
    try {
        test::require(argc >= 2, "explicit crash probe mode required");
        const std::string mode = argv[1];
        const auto config = c::tests::migu::support::makeMySqlConfigFromEnv();
        test::require(config.database.starts_with("crawler_ownership_test_crash_") && c::tests::migu::support::safeCoverDatabaseName(config.database), "refusing non-isolated crash database");
        // 仅setup执行迁移；worker不能复用会重复获取全局迁移锁的测试helper。
        auto pool = mode == "setup" ? c::tests::migu::support::makeCoverTestPool() : std::make_shared<mysql::MySqlConnectionPool>(config);
        if (mode == "setup" && argc == 2)
            setup(pool);
        else if (mode == "hold" && argc == 4)
            hold(pool, std::filesystem::u8path(argv[2]), argv[3]);
        else if (mode == "recover" && argc == 2)
            recover(pool);
        else if (mode == "repair" && argc == 4)
            repair(pool, std::filesystem::u8path(argv[2]), argv[3]);
        else
            throw std::runtime_error("invalid crash probe mode");
        return 0;
    } catch (const std::exception &) {
        std::cerr << "FAIL: isolated crash probe assertion or dependency failed\n";
        return 1;
    }
}
