#include "crawler/infrastructure/app/ResourceTransferFactory.h"

#include "crawler/infrastructure/app/ResourceTargetVerifier.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace app = c::infrastructure::app;
namespace model = c::application::model;
namespace http = c::infrastructure::http::client;
namespace files = c::infrastructure::storage::filesystem;
using Json = nlohmann::json;
using Destination = model::ResourceDestination;
using Clock = std::chrono::steady_clock;
namespace {
// 原始路径只供进程内使用，公开记录只写安全摘要。
std::string utf8(const std::filesystem::path &path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
// 缺少显式环境时失败，不加载工作区或原 checkout 的 dotenv。
std::string required(const std::string &key)
{
    const auto value = c::infrastructure::platform::Environment::getString(key);
    if (!value || value->empty()) throw std::invalid_argument("required_live_configuration_missing");
    return *value;
}
// 只记录相对本轮开始的真实调用时序，不记录 URL、认证或响应正文。
class ObservedClient final : public http::AsioHttpClient {
public:
    // 每端独立 I/O 客户端；共享时钟只用于比较观察时序。
    explicit ObservedClient(Clock::time_point epoch) : epoch_(epoch)
    {}
    // 透传真实请求，只在请求体被实际消费时记录上传开始。
    http::HttpUploadResult streamPostWithOptions(std::string_view url, http::HttpUploadBody body, const std::vector<std::string> &headers, const http::HttpRequestOptions &options) override
    {
        auto unset = std::int64_t {-1};
        first_request_us.compare_exchange_strong(unset, elapsed());
        const bool upload = url.find("/operations/uploadfile?") != std::string_view::npos;
        if (upload) {
            auto reader = std::move(body.read);
            body.read = [this, reader = std::move(reader)](std::span<char> bytes) {
                const auto count = reader(bytes);
                if (count && *count) {
                    auto missing = std::int64_t {-1};
                    first_upload_body_us.compare_exchange_strong(missing, elapsed());
                }
                return count;
            };
        }
        auto result = http::AsioHttpClient::streamPostWithOptions(url, std::move(body), headers, options);
        if (upload) {
            upload_request_started = result.request_started;
            upload_bytes_sent = result.bytes_sent;
        }
        return result;
    }
    // 各字段由执行线程或 I/O 线程更新，观察方只做原子读取。
    std::atomic<std::int64_t> first_request_us {-1};     // 首次 RC 调用
    std::atomic<std::int64_t> first_upload_body_us {-1}; // 首次 multipart 正文供给
    std::atomic<std::uint64_t> upload_bytes_sent {0};    // 客户端确认的实际发送量
    std::atomic_bool upload_request_started {false};     // 已越过请求头发送边界
private:
    // 单调时钟不受系统时钟校准影响。
    std::int64_t elapsed() const
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch_).count();
    }
    Clock::time_point epoch_; // 本轮固定起点
};
// 不删除输入文件；由驱动保留本轮完整样本和证据。
class Snapshot final : public model::ResourceContentSnapshot {
public:
    // 每端拥有独立样本路径，完整计算大小及 SHA-256。
    explicit Snapshot(const std::filesystem::path &path) : path_(utf8(path))
    {
        const auto value = files::digestResourceFile(path, 1024 * 1024, {Clock::now() + std::chrono::seconds(10), {}});
        if (!value.isSuccess()) throw std::runtime_error("live_snapshot_digest_failed");
        digest_ = *value.data();
    }
    // 只向正式存储 port 提供内部快照路径。
    const std::string &filePath() const override
    {
        return path_;
    }
    // 固定安全相对名，不从真实音乐源取得正文。
    const std::string &relativePath() const override
    {
        return relative_;
    }
    // 使用实际文件完整 SHA-256。
    const std::string &checksum() const override
    {
        return digest_.sha256;
    }
    // 使用实际完整文件字节数。
    std::uint64_t sizeBytes() const override
    {
        return digest_.size_bytes;
    }

private:
    std::string path_;                              // 独占本轮样本
    std::string relative_ = "controlled/probe.bin"; // 安全布局
    files::ResourceFileDigest digest_;              // 实测摘要
};
// 逐端参数仅由父进程环境传入，敏感值永不序列化。
c::infrastructure::storage::rclone::RcloneConfig settings(const std::string &name)
{
    c::infrastructure::storage::rclone::RcloneConfig value;
    const auto prefix = "CRAWLER_CLOUD_LIVE_" + name;
    value.rc_url = required(prefix + "_RC_URL");
    value.rc_user = required(prefix + "_RC_USER");
    value.rc_password = required(prefix + "_RC_PASSWORD");
    value.target_fs = required(prefix + "_TARGET_FS");
    value.api_timeout_ms = 10000;
    return value;
}
// 身份检查只调用正式 verifier，失败时不能继续写云端。
Json identity(const app::ServerApplication::Config &config, const std::array<app::ResourceCloudTarget, 2> &targets, bool gdrive_only)
{
    const model::ResourceTransferContext context {Clock::now() + std::chrono::seconds(45), {}};
    const auto local = app::verifyLocalResourceTarget(std::filesystem::u8path(config.storage_path), context);
    Json result = Json::array();
    auto add = [&](Destination destination, const app::ResourceTargetVerification &value) {
        result.push_back({{"destination", model::resourceDestinationName(destination)}, {"available", value.profile.available}, {"fingerprint", value.profile.fingerprint}, {"identity_scope", value.identity_scope}, {"result_code", value.result_code}});
    };
    add(Destination::Local, local);
    for (const auto &target : targets) {
        if (gdrive_only && target.provider == Destination::Baidu) continue;
        add(target.provider, app::verifyCloudResourceTarget(config, target, context));
    }
    return result;
}
// 认领模型只用于真实工厂 I/O，不声称执行过数据库或服务 worker。
model::ResourceTransferJob job(Destination destination)
{
    model::ResourceTransferJob value;
    value.destination = destination;
    value.adapter = destination == Destination::Local ? "filesystem" : "rclone";
    value.resource = {model::ResourceOwner::Singer, 1};
    value.task_id = 1 + static_cast<std::uint64_t>(destination);
    value.claim_generation = value.native_claim_generation = 1;
    value.state = "running";
    value.policy.max_bytes = 1024 * 1024;
    value.policy.bytes_per_second = destination == Destination::Local ? 32768 : 131072;
    value.policy.strict_sha256 = true;
    return value;
}
} // namespace

// 真实云端探针必须由显式驱动运行，不作为普通 CTest。
int main(int argc, char **argv)
{
    Json report {{"execution", "actual_factory_io"}, {"database_connected", false}, {"service_workers_tested", false}, {"source_content_requests", 0}, {"remote_deletes", 0}};
    try {
        if (argc != 3) throw std::invalid_argument("usage_mode_and_private_work_directory_required");
        const std::string mode = argv[1];
        if (mode != "verify" && mode != "parallel" && mode != "cancel-baidu" && mode != "cancel-gdrive" && mode != "verify-gdrive" && mode != "gdrive-only" && mode != "cancel-gdrive-only") throw std::invalid_argument("invalid_probe_mode");
        const bool gdrive_only = mode == "verify-gdrive" || mode == "gdrive-only" || mode == "cancel-gdrive-only";
        report["enabled_destinations"] = gdrive_only ? Json {"local", "gdrive"} : Json {"local", "baidu", "gdrive"};
        report["baidu_tested"] = !gdrive_only;
        const auto root = std::filesystem::u8path(argv[2]);
        if (!std::filesystem::is_directory(root / "local")) throw std::invalid_argument("existing_private_local_root_required");
        const auto epoch = Clock::now();
        app::ServerApplication::Config config;
        config.storage_path = utf8(root / "local");
        config.resource_cloud_upload_backend = "rclone";
        std::array<app::ResourceCloudTarget, 2> targets {{{Destination::Baidu, {}, 8 * 1024 * 1024, "rclone", settings("BAIDU"), {}}, {Destination::Gdrive, {}, 8 * 1024 * 1024, "rclone", settings("GDRIVE"), {}}}};
        report["mode"] = mode;
        report["targets_before"] = identity(config, targets, gdrive_only);
        bool available = true;
        std::set<std::string> fingerprints;
        for (const auto &target : report["targets_before"]) {
            available = available && target["available"].get<bool>();
            fingerprints.insert(target["fingerprint"].get<std::string>());
        }
        available = available && fingerprints.size() == (gdrive_only ? 2 : 3);
        if (!available) {
            report["passed"] = false;
            report["result_code"] = "real_target_identity_unavailable";
            std::cout << report.dump() << '\n';
            return 2;
        }
        if (mode == "verify" || mode == "verify-gdrive") {
            report["passed"] = true;
            std::cout << report.dump() << '\n';
            return 0;
        }
        if (!std::filesystem::is_empty(root / "local") || !std::filesystem::create_directory(root / "attempts")) throw std::invalid_argument("fresh_live_work_directory_required");
        for (const auto &target : targets) {
            if (gdrive_only && target.provider == Destination::Baidu) continue;
            const auto &fs = target.rclone->target_fs;
            const auto slash = fs.find_last_of("/:");
            if (slash == std::string::npos || !fs.substr(slash + 1).starts_with("crawler-cloud-live-")) throw std::invalid_argument("exclusive_live_cloud_prefix_required");
        }
        std::array<std::shared_ptr<ObservedClient>, 2> clients {std::make_shared<ObservedClient>(epoch), std::make_shared<ObservedClient>(epoch)};
        for (std::size_t index = 0; index < targets.size(); ++index) targets[index].rclone_client = clients[index];
        const auto assembly = app::ResourceTransferFactory(config).build(true, gdrive_only ? std::vector<app::ResourceCloudTarget> {targets[1]} : std::vector<app::ResourceCloudTarget> {targets.begin(), targets.end()});
        std::array<std::atomic_bool, 3> cancelled {};
        std::promise<void> start;
        const auto ready = start.get_future().share();
        std::array<std::future<Json>, 3> lanes;
        // 启动线程失败时也释放屏障，避免 future 析构永久等待。
        struct StartGuard {
            std::promise<void> &start; // 只管理本轮屏障
            ~StartGuard()
            {
                try {
                    start.set_value();
                } catch (...) {}
            }
        } release {start};
        for (std::size_t index = 0; index < lanes.size(); ++index) {
            if (gdrive_only && index == 1) continue;
            lanes[index] = std::async(std::launch::async, [&, index] {
                const auto destination = static_cast<Destination>(index);
                const auto transfer = assembly.destination(destination);
                if (!transfer) throw std::runtime_error("live_factory_destination_unavailable");
                const auto snapshot = std::make_shared<Snapshot>(root / ("source-" + std::to_string(index) + ".bin"));
                ready.wait();
                const model::ResourceTransferContext context {Clock::now() + std::chrono::seconds(120), [&, index] { return cancelled[index].load(); }};
                Json item {{"destination", model::resourceDestinationName(destination)}, {"started_us", std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count()}};
                const auto prepared = transfer->prepare(job(destination), snapshot, context);
                if (!prepared.isSuccess()) {
                    item["passed"] = false;
                    item["result_code"] = prepared.message();
                    return item;
                }
                auto session = *prepared.data();
                item["attempt_path_sha256"] = c::infrastructure::storage::cloud::sha256(session->attempt().path);
                std::ofstream manifest(root / "attempts" / (std::to_string(index) + ".json"), std::ios::binary);
                manifest << Json {{"destination", model::resourceDestinationName(destination)}, {"path", session->attempt().path}, {"sha256", snapshot->checksum()}, {"size_bytes", snapshot->sizeBytes()}}.dump() << '\n';
                manifest.close();
                if (!manifest) throw std::runtime_error("private_attempt_manifest_failed");
                const auto receipt = session->publish(context);
                item["finished_us"] = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count();
                item["passed"] = receipt.isSuccess();
                item["result_code"] = receipt.message();
                if (receipt.isSuccess()) {
                    item["sha256"] = receipt.data()->checksum;
                    item["size_bytes"] = receipt.data()->size_bytes;
                    item["uploaded"] = receipt.data()->uploaded;
                    item["verification_method"] = receipt.data()->verification_method;
                    item["sha256_verified"] = receipt.data()->verification == model::ResourceVerification::Sha256Verified;
                    session->settle(model::ResourceTransferSettlement::Accept);
                } else
                    session->settle(model::ResourceTransferSettlement::Defer);
                return item;
            });
        }
        start.set_value();
        const int cancelled_lane = mode == "cancel-baidu" ? 1 : mode == "cancel-gdrive" || mode == "cancel-gdrive-only" ? 2 :
                                                                                                                          -1;
        if (cancelled_lane > 0) {
            const auto until = Clock::now() + std::chrono::seconds(45);
            while (clients[cancelled_lane - 1]->first_upload_body_us.load() < 0 && Clock::now() < until && lanes[cancelled_lane].wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
            cancelled[cancelled_lane] = true;
            report["cancel_requested_us"] = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count();
        }
        report["lanes"] = Json::array();
        for (std::size_t index = 0; index < lanes.size(); ++index) {
            if (gdrive_only && index == 1)
                report["lanes"].push_back({{"destination", "baidu"}, {"enabled", false}, {"tested", false}});
            else
                report["lanes"].push_back(lanes[index].get());
        }
        bool passed = true;
        if (cancelled_lane > 0) {
            const auto latency = report["lanes"][cancelled_lane].value("finished_us", std::int64_t {-1}) - report["cancel_requested_us"].get<std::int64_t>();
            report["cancel_stop_latency_us"] = latency;
            report["cancel_stop_limit_ms"] = 5000;
            passed = latency >= 0 && latency <= 5000000;
        }
        for (std::size_t index = 0; index < clients.size(); ++index) {
            if (gdrive_only && index == 0) continue;
            auto &lane = report["lanes"][index + 1];
            lane["first_request_us"] = clients[index]->first_request_us.load();
            lane["first_upload_body_us"] = clients[index]->first_upload_body_us.load();
            lane["upload_bytes_sent"] = clients[index]->upload_bytes_sent.load();
            lane["upload_request_started"] = clients[index]->upload_request_started.load();
        }
        const auto local_finished = report["lanes"][0].value("finished_us", std::int64_t {-1});
        const bool overlapped = local_finished > 0 && clients[1]->first_upload_body_us >= 0 && clients[1]->first_upload_body_us < local_finished && (gdrive_only || (clients[0]->first_upload_body_us >= 0 && clients[0]->first_upload_body_us < local_finished));
        report["enabled_clouds_started_before_local_finished"] = overlapped;
        report["clouds_started_before_local_finished"] = !gdrive_only && overlapped;
        for (int index = 0; index < 3; ++index) {
            if (gdrive_only && index == 1) continue;
            passed = passed && (index == cancelled_lane ? !report["lanes"][index]["passed"].get<bool>() && report["lanes"][index].value("upload_bytes_sent", 0ULL) > 0 : report["lanes"][index]["passed"].get<bool>() && report["lanes"][index].value("sha256_verified", false));
        }
        report["targets_after"] = identity(config, targets, gdrive_only);
        report["target_identity_unchanged"] = report["targets_before"] == report["targets_after"];
        report["passed"] = passed && report["target_identity_unchanged"].get<bool>() && overlapped;
        report["three_way_passed"] = !gdrive_only && report["passed"].get<bool>();
        std::cout << report.dump() << '\n';
        return report["passed"].get<bool>() ? 0 : 2;
    } catch (const std::exception &) {
        report["passed"] = false;
        report["result_code"] = "live_probe_failed";
        std::cout << report.dump() << '\n';
        return 1;
    }
}
