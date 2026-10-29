#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "MetadataCoverIo.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {

// 路径片段不接受分隔符、相对目录或平台保留字符。
bool safeSegment(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// 保存脱敏的失败结果，原始远端地址不能进入错误记录。
application::model::MetadataResourceFile failure(std::string category, std::string code, std::string message)
{
    application::model::MetadataResourceFile result;
    result.status = 3;
    result.error_category = std::move(category);
    result.result_code = std::move(code);
    result.last_error = std::move(message);
    return result;
}

} // namespace

MetadataCoverStore::MetadataCoverStore(std::shared_ptr<http::client::HttpClient> client, std::shared_ptr<FileStorage> files, std::vector<std::string> headers) : client_(std::move(client)), files_(std::move(files)), headers_(std::move(headers))
{}

application::model::MetadataResourceFile MetadataCoverStore::save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url)
{
    if (owner_kind != "singers" && owner_kind != "albums") return failure("invalid_target", "invalid_cover_target", "invalid metadata cover target");
    application::model::MetadataCoverSaveRequest request;
    request.target.owner = owner_kind == "singers" ? application::model::CoverOwner::Singer : application::model::CoverOwner::Album;
    request.target.source = source;
    request.target.source_id = source_id;
    request.target.remote_url = url;
    return save(std::move(request));
}

application::model::MetadataResourceFile MetadataCoverStore::save(application::model::MetadataCoverSaveRequest input)
{
    const auto &target = input.target;
    if (!client_ || !files_ || !safeSegment(target.source) || !safeSegment(target.source_id)) {
        return failure("invalid_target", "invalid_cover_target", "invalid metadata cover target");
    }
    auto fetched = cover_io::fetch(*client_, target.remote_url, headers_, input.timeout_ms, input.cancelled);
    if (fetched.error.status == 3) return fetched.error;
    if (input.cancelled && input.cancelled()) return failure("resource", "cancelled", "cover processing cancelled");
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const std::string owner_directory = target.owner == application::model::CoverOwner::Singer ? "singers" : "albums";
    application::model::SongResourceBlobStreamSaveRequest request;
    request.require_database_ack = true;
    request.resource_id = target.resource_id;
    request.claim_generation = target.claim_generation;
    request.owner_kind = target.resource_id == 0 ? "song" : std::string(application::model::coverOwnerName(target.owner));
    request.previous_local_path = target.file.local_path;
    request.relative_path_hint = target.source + "/" + owner_directory + "/" + target.source_id + "/cover/" + std::to_string(stamp) + "-" + std::to_string(sequence_.fetch_add(1)) + "." + fetched.extension;
    request.content_type = cover_io::imageMime(fetched.extension);
    auto session = files_->beginSongResourceBlobWrite(std::move(request));
    std::string error;
    if (!session || !session->write(std::string_view(fetched.bytes.data(), fetched.bytes.size()), error)) {
        return failure("persistence", "cover_save_failed", "metadata cover file could not be staged");
    }
    auto saved = session->commit();
    if (!saved.ok) return failure("persistence", "cover_save_failed", "metadata cover file could not be committed");
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.emplace(saved.local_path, std::move(session));
    }
    application::model::MetadataResourceFile result;
    result.local_path = std::move(saved.local_path);
    result.size_bytes = saved.size_bytes;
    result.checksum = std::move(saved.checksum);
    result.status = 2;
    result.result_code = "ok";
    return result;
}
bool MetadataCoverStore::discard(std::string_view local_path)
{
    if (auto session = takeSession(local_path)) {
        session->abort();
        return !files_->exists(local_path);
    }
    return files_ && !local_path.empty() && files_->remove(local_path);
}

std::unique_ptr<application::ports::SongResourceBlobWriteSession> MetadataCoverStore::takeSession(std::string_view local_path)
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    const auto found = sessions_.find(std::string(local_path));
    if (found == sessions_.end()) return nullptr;
    auto session = std::move(found->second);
    sessions_.erase(found);
    return session;
}

void MetadataCoverStore::accept(std::string_view local_path)
{
    if (auto session = takeSession(local_path)) session->accept();
}

void MetadataCoverStore::defer(std::string_view local_path)
{
    if (auto session = takeSession(local_path)) session->defer();
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
