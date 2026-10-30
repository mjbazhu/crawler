#pragma once

#include "crawler/infrastructure/storage/rclone/RcloneResourceTransfer.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <nlohmann/json.hpp>

#include <string>
#include <cstdint>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer {
namespace model = application::model;
namespace result = application::result;

// 小型 RPC 也使用可取消连接，不借用全局超时或全局限速设置。
result::ServiceResult<nlohmann::json> post(http::client::HttpClient &client, const RcloneConfig &config, std::string endpoint, nlohmann::json body, const model::ResourceTransferContext &context, bool readback = false, std::uint64_t bytes_per_second = 0);
// 完整响应才参与协议解析；不向上层传递响应正文或认证。
result::ServiceResult<nlohmann::json> parse(const http::client::HttpUploadResult &response);
// 已知 MD5 仅用于新快照；旧意图没有 MD5 时只用远端 SHA-256 或实际回读。
result::ServiceResult<model::ResourceAttemptInspection> verify(http::client::HttpClient &client, const RcloneConfig &config, const model::ResourceTransferAttempt &attempt, std::string md5, const model::ResourceTransferContext &context, bool allow_absent, std::uint64_t bytes_per_second = 0);
// 前缀、文件和结尾按需提供，读取期间复核 SHA-256，单个缓冲不超过 64 KiB。
http::client::HttpUploadBody multipart(const model::ResourceContentSnapshot &snapshot, std::string filename, std::string boundary, const model::ResourceTransferContext &context, std::uint64_t bytes_per_second);
// 已登记的独占对象发布与核验，只有成功后才标记本次实际上传。
result::ServiceResult<model::ResourceTransferReceipt> publish(http::client::HttpClient &client, const RcloneConfig &config, const model::ResourceTransferAttempt &attempt, const model::ResourceContentSnapshot &snapshot, const filesystem::ResourceFileDigest &digest, const model::ResourceTransferContext &context, std::uint64_t bytes_per_second);

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer
