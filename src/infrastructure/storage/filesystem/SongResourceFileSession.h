#pragma once
#include "crawler/application/model/SongResourceFile.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include <filesystem>
#include <memory>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace file_detail {
// 打开带独占临时文件与可选数据库确认日志的写入会话。
std::unique_ptr<application::ports::SongResourceBlobWriteSession> openSongResourceWrite(const std::filesystem::path &root_dir, application::model::SongResourceBlobStreamSaveRequest request);
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::file_detail
