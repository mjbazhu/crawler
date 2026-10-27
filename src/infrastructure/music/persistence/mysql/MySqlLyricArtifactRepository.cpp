#include "crawler/infrastructure/music/persistence/mysql/MySqlLyricArtifactRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>
#include <boost/system/system_error.hpp>

#include <exception>
#include <future>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {

// 日志只记录阶段、资源 ID 和错误码，不记录连接参数、SQL 参数或密钥。
void logIndexFailure(std::string_view stage, std::uint64_t resource, boost::system::error_code code = {})
{
    const std::string message = "stage=" + std::string(stage) + " resource_id=" + std::to_string(resource) + " error=" + std::string(code.category().name()) + ':' + std::to_string(code.value());
    observability::logging::Logger::instance().log(observability::logging::LogLevel::L_ERROR, "LyricArtifactRepository", message);
}

} // namespace

MySqlLyricArtifactRepository::MySqlLyricArtifactRepository(std::shared_ptr<MySqlConnectionPool> pool) :
    pool_(std::move(pool)),
    select_source_sql_(sql::SqlFileLoader::loadOrThrow("sql/mysql/song_resource_artifacts/select_source.sql")),
    upsert_sql_(sql::SqlFileLoader::loadOrThrow("sql/mysql/song_resource_artifacts/upsert.sql"))
{}

application::result::ServiceResult<bool> MySqlLyricArtifactRepository::record(const application::model::LyricArtifactBundle &bundle)
{
    if (bundle.files.size() != 2) return application::result::ServiceResult<bool>::failure(application::result::ServiceErrorType::InvalidParam, "lyric_invalid_artifact_index");
    return wait(bundle, {});
}

application::result::ServiceResult<bool> MySqlLyricArtifactRepository::recordFailure(const application::model::LyricProcessingRequest &request, std::string_view version, int schema_version, std::string_view code)
{
    application::model::LyricArtifactBundle bundle {request.source_resource_id, request.expected_checksum, std::string(version), schema_version, {}};
    bundle.files.push_back({"word_json", {}, 0, {}});
    bundle.files.push_back({"enhanced_lrc", {}, 0, {}});
    return wait(std::move(bundle), std::string(code));
}

application::result::ServiceResult<bool> MySqlLyricArtifactRepository::wait(application::model::LyricArtifactBundle bundle, std::string failure)
{
    using Result = application::result::ServiceResult<bool>;
    if (!pool_) return Result::failure(application::result::ServiceErrorType::InternalError, "lyric_index_not_configured");
    const auto resource = bundle.source_resource_id;
    try {
        // Asio 的异常完成分支需要可默认构造的传输值，optional 只用于该技术边界。
        auto received = boost::asio::co_spawn(pool_->executor(), coRecord(std::move(bundle), std::move(failure)), boost::asio::use_future).get();
        return received.value_or(Result::failure(application::result::ServiceErrorType::InternalError, "lyric_index_write_failed"));
    } catch (const boost::system::system_error &error) {
        logIndexFailure("dispatch", resource, error.code());
        return Result::failure(application::result::ServiceErrorType::InternalError, "lyric_index_write_failed");
    } catch (const std::exception &) {
        logIndexFailure("dispatch", resource);
        return Result::failure(application::result::ServiceErrorType::InternalError, "lyric_index_write_failed");
    }
}

boost::asio::awaitable<std::optional<application::result::ServiceResult<bool>>> MySqlLyricArtifactRepository::coRecord(application::model::LyricArtifactBundle bundle, std::string failure)
{
    using Result = application::result::ServiceResult<bool>;
    using Error = application::result::ServiceErrorType;
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results rows;
    bool stale = false;
    std::string_view stage = "begin";
    try {
        co_await connection->async_execute("START TRANSACTION", rows, boost::asio::use_awaitable);
        stage = "source_check";
        const auto source = co_await connection->async_prepare_statement(select_source_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(source.bind(bundle.source_resource_id), rows, boost::asio::use_awaitable);
        stale = rows.rows().empty() || bundle.source_checksum.empty() || MySqlValueReader::stringAt(rows.rows().at(0), 0) != bundle.source_checksum;
        if (!stale) {
            stage = "upsert";
            const auto statement = co_await connection->async_prepare_statement(upsert_sql_, boost::asio::use_awaitable);
            const int status = failure.empty() ? 2 : 3;
            const std::string code = failure.empty() ? "ok" : failure;
            for (const auto &file : bundle.files) {
                co_await connection->async_execute(statement.bind(bundle.source_resource_id, file.kind, bundle.source_checksum, bundle.processor_version, bundle.schema_version, file.local_path, file.size_bytes, file.checksum, status, code), rows, boost::asio::use_awaitable);
            }
            stage = "commit";
            co_await connection->async_execute("COMMIT", rows, boost::asio::use_awaitable);
            co_return Result::success(true);
        }
    } catch (const boost::system::system_error &error) {
        logIndexFailure(stage, bundle.source_resource_id, error.code());
    } catch (const std::exception &) {
        logIndexFailure(stage, bundle.source_resource_id);
        // 连接由 pooled_connection 托管，回滚后才允许返还池。
    }
    boost::system::error_code rollback_error;
    co_await connection->async_execute("ROLLBACK", rows, boost::asio::redirect_error(boost::asio::use_awaitable, rollback_error));
    if (rollback_error) logIndexFailure("rollback", bundle.source_resource_id, rollback_error);
    co_return Result::failure(stale ? Error::Conflict : Error::InternalError, stale ? "lyric_source_changed" : "lyric_index_write_failed");
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
