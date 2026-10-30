#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace orchestrator::modules::crawler::tests::retired {
namespace model = application::model;
namespace mysql = infrastructure::music::persistence::mysql;

// 模拟收缩迁移保留的旧云证据；生产档案不可变，fixture显式覆盖仅用于构造测试。
inline void cloudArchive(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, model::ResourceIdentity identity, nlohmann::json values)
{
    nlohmann::json defaults {{"cloud_backend", ""}, {"cloud_path", nullptr}};
    if (identity.owner != model::ResourceOwner::SongArtifact) defaults["cloud_uploaded_at"] = 0;
    if (identity.owner == model::ResourceOwner::Singer || identity.owner == model::ResourceOwner::Album) {
        for (const auto key : {"cloud_size_bytes", "cloud_status", "cloud_claim_generation", "cloud_claimed_at"}) defaults[key] = 0;
        for (const auto key : {"cloud_checksum", "cloud_error_category", "cloud_result_code"}) defaults[key] = "";
        for (const auto key : {"cloud_running_batch_id", "cloud_last_error", "cloud_attempt"}) defaults[key] = nullptr;
    }
    for (const auto &[key, value] : values.items()) defaults[key] = value;
    const auto preview = mysql::MySqlLegacyResourceImport(pool).preview(identity.owner, identity.resource_id - 1, identity.resource_id, 1);
    if (!preview.isSuccess() || preview.data()->rows.size() != 1) throw std::runtime_error("retired cloud fixture resource missing");
    const auto source = preview.data()->rows.front();
    auto write = [pool, source, payload = defaults.dump()]() -> boost::asio::awaitable<bool> {
        auto connection = co_await pool->coAcquire();
        auto statement = co_await connection->async_prepare_statement("INSERT INTO resource_cloud_migration_audit(run_id,owner_type,resource_id,source_identity,content_generation,old_values,disposition,created_at) VALUES('schema-retirement-v2026102913',?,?,?,?,CAST(? AS JSON),'archived',UNIX_TIMESTAMP()) ON DUPLICATE KEY UPDATE old_values=VALUES(old_values)", boost::asio::use_awaitable);
        const std::string owner(model::resourceOwnerName(source.identity.owner));
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(owner, source.identity.resource_id, source.source_identity, source.generation, payload), result, boost::asio::use_awaitable);
        co_return true;
    };
    if (!migu::support::runOnExecutor(pool->executor(), write(), "retired cloud fixture")) throw std::runtime_error("retired cloud fixture failed");
}
} // namespace orchestrator::modules::crawler::tests::retired
