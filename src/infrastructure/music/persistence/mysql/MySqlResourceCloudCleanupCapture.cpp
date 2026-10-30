#include "MySqlResourceCloudCleanupCapture.h"

#include "MySqlResourceTransferJobsSupport.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup {
namespace jobs = transfer_jobs;
namespace model = application::model;
namespace {
// 只摘要数据库快照，不读取媒体或输出路径。
std::string digest(const std::string &text)
{
    std::array<unsigned char, 32> bytes {};
    unsigned size = 0;
    if (EVP_Digest(text.data(), text.size(), bytes.data(), &size, EVP_sha256(), nullptr) != 1 || size != bytes.size()) throw std::runtime_error("cleanup digest failed");
    constexpr auto hex = "0123456789abcdef";
    std::string value;
    for (const auto byte : bytes) {
        value += hex[byte >> 4];
        value += hex[byte & 15];
    }
    return value;
}

// 删除不能吞掉在途或未决任务；锁定读避免RR旧快照漏掉已提交占用。
boost::asio::awaitable<void> guard(boost::mysql::pooled_connection &connection, const jobs::Snapshot &snapshot, CaptureReason reason)
{
    // 既有别名业务在删除旧主键后拒绝旧worker回写，规范资源的活跃本地认领保持原状。
    if (reason == CaptureReason::AlbumAlias && snapshot.identity.owner != model::ResourceOwner::Album) throw std::invalid_argument("invalid album alias capture");
    if (reason != CaptureReason::AlbumAlias && (snapshot.status == 1 || !snapshot.running_batch_id.empty())) throw std::logic_error("resource deletion requires reconciliation");
    const auto owner = std::string(model::resourceOwnerName(snapshot.identity.owner));
    for (const auto *path : {"resource_transfer_slots/deletion_guard", "resource_transfer_tasks/deletion_guard", "resource_producer_tasks/deletion_guard"}) {
        const auto rows = co_await jobs::query(connection, path, owner, snapshot.identity.resource_id);
        if (!rows.rows().empty()) throw std::logic_error("resource deletion requires reconciliation");
    }
    if (snapshot.identity.owner == model::ResourceOwner::Singer || snapshot.identity.owner == model::ResourceOwner::Album) {
        const auto rows = co_await jobs::query(connection, jobs::table(snapshot.identity.owner) + "/deletion_cloud_guard", snapshot.identity.resource_id);
        if (!rows.rows().empty()) throw std::logic_error("resource deletion requires reconciliation");
    }
}

// 不可变退役档案继续保留原工具/弱摘要；它不代表当前确认，不猜测rclone所属平台。
boost::asio::awaitable<bool> archiveLegacy(boost::mysql::pooled_connection &connection, const jobs::Snapshot &source)
{
    const auto rows = co_await jobs::query(connection, jobs::table(source.identity.owner) + "/deletion_legacy_snapshot", source.identity.resource_id);
    if (rows.rows().empty()) co_return false;
    const auto row = rows.rows().front();
    const auto backend = MySqlValueReader::stringAt(row, 0), path = MySqlValueReader::stringAt(row, 1);
    if (backend.empty() && path.empty()) co_return false;
    const auto payload = nlohmann::json {{"content_generation", source.generation}, {"source_identity", source.source_identity}, {"source_revision", source.source_revision}, {"local_path", model::safeResourceCloudCleanupPath(source.local_path) ? source.local_path : std::string {}}, {"local_path_sha256", digest(source.local_path)}, {"checksum", source.checksum}, {"size_bytes", source.size_bytes}, {"cloud_backend", backend}, {"cloud_path", model::safeResourceCloudCleanupPath(path) ? path : std::string {}}, {"cloud_path_sha256", digest(path)}, {"retired_cloud_content_generation", MySqlValueReader::uint64At(row, 4)}, {"retired_cloud_source_identity", MySqlValueReader::stringAt(row, 5)}, {"cloud_checksum", MySqlValueReader::stringAt(row, 2)}, {"cloud_size_bytes", MySqlValueReader::uint64At(row, 3)}, {"verification", "legacy_unverified"}}.dump();
    const auto result = co_await jobs::query(connection, "resource_cloud_migration_audit/deletion_archive", "delete:" + digest(payload), std::string(model::resourceOwnerName(source.identity.owner)), source.identity.resource_id, source.source_identity, source.generation, payload);
    co_return result.affected_rows() == 1;
}
} // namespace

boost::asio::awaitable<model::ResourceCloudCleanupCapture> captureOnConnection(boost::mysql::pooled_connection &connection, std::vector<model::ResourceIdentity> resources, CaptureReason reason)
{
    const bool deleting = reason != CaptureReason::ExplicitMaintenance;
    if (resources.empty() || resources.size() > (deleting ? 10000 : 200)) throw std::invalid_argument("invalid cleanup scope");
    std::sort(resources.begin(), resources.end(), [](auto a, auto b) { return a.owner < b.owner || (a.owner == b.owner && a.resource_id < b.resource_id); });
    if (std::adjacent_find(resources.begin(), resources.end()) != resources.end()) throw std::invalid_argument("duplicate cleanup identity");
    std::vector<std::optional<jobs::Snapshot>> snapshots;
    for (const auto id : resources) {
        if (!id.resource_id) throw std::invalid_argument("invalid cleanup identity");
        snapshots.push_back(co_await jobs::snapshot(connection, id));
    }
    const auto targets = co_await jobs::targets(connection);
    if (!deleting)
        for (const auto &target : targets)
            if (target.state != "maintenance") throw jobs::WriteAdmissionClosed();
    model::ResourceCloudCleanupCapture result;
    std::size_t archived_legacy = 0;
    for (std::size_t i = 0; i < resources.size(); ++i) {
        if (!snapshots[i]) {
            ++result.resources_missing;
            continue;
        }
        const auto identity = resources[i];
        const auto &source = *snapshots[i];
        if (deleting) {
            co_await guard(connection, source, reason);
            if (co_await archiveLegacy(connection, source)) ++archived_legacy;
        }
        const auto owner = std::string(model::resourceOwnerName(identity.owner));
        const auto rows = co_await jobs::query(connection, "resource_cloud_cleanup_candidates/capture_receipts", owner, identity.resource_id);
        if (rows.rows().empty()) ++result.resources_without_receipts;
        for (const auto row : rows.rows()) {
            const auto receipt = MySqlValueReader::uint64At(row, 0);
            const auto provider = MySqlValueReader::stringAt(row, 1);
            const auto parsed = model::parseCloudProvider(provider);
            if (!parsed) throw std::logic_error("invalid cleanup provider");
            const auto &target = targets.at(static_cast<std::size_t>(*parsed));
            const auto confirmed_generation = MySqlValueReader::uint64At(row, 2), confirmed_target = MySqlValueReader::uint64At(row, 3), bytes = MySqlValueReader::uint64At(row, 5);
            const auto path = MySqlValueReader::stringAt(row, 4), checksum = MySqlValueReader::stringAt(row, 6), verification = MySqlValueReader::stringAt(row, 7), adapter = MySqlValueReader::stringAt(row, 8), state = MySqlValueReader::stringAt(row, 9);
            if (deleting && state == "uncertain") throw std::logic_error("resource deletion requires reconciliation");
            const auto key = MySqlValueReader::intAt(row, 10) ? std::string {} : model::resourceCloudCleanupObjectKey(path);
            const auto path_hash = digest(path);
            const auto retained_path = model::safeResourceCloudCleanupPath(path) ? path : std::string {};
            const auto payload = nlohmann::json::array({receipt, owner, identity.resource_id, source.generation, provider, target.revision, target.fingerprint, confirmed_generation, confirmed_target, path_hash, key, bytes, checksum, verification, adapter, state});
            const auto inserted = co_await jobs::query(connection, "resource_cloud_cleanup_candidates/insert", receipt, owner, identity.resource_id, source.generation, provider, target.revision, target.fingerprint, confirmed_generation, confirmed_target, retained_path, path_hash, key, bytes, checksum, verification, adapter, state, digest(payload.dump()));
            result.candidate_ids.push_back(inserted.last_insert_id());
            if (inserted.affected_rows() == 1) ++result.inserted;
        }
    }
    // 捕获和删除由所属事务共同提交；同快照不重复写审计或改变时间戳。
    if (result.inserted || archived_legacy) {
        const auto payload = nlohmann::json {{"candidate_ids", result.candidate_ids}, {"new_snapshots", result.inserted}, {"legacy_archives", archived_legacy}, {"writers_stopped", !deleting}, {"deletion_transaction", deleting}, {"remote_deletions", 0}}.dump();
        (void)co_await jobs::query(connection, "governance_events/cloud_cleanup_capture", payload);
    }
    co_return result;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup
