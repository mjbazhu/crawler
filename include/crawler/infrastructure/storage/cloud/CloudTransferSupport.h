#pragma once

#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::storage::cloud {

// 已验证快照与持久独占对象交给具体后端，工厂/会话不替代后端实测。
using CloudPublisher = std::function<application::result::ServiceResult<application::model::ResourceTransferReceipt>(const application::model::ResourceTransferAttempt &, const application::model::ResourceContentSnapshot &, const filesystem::ResourceFileDigest &, const application::model::ResourceTransferContext &)>;

// 校验实际文件并冻结独占版本路径；此阶段无远端操作，保留快照租约到会话结束。
application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> prepareCloudTransfer(const application::model::ResourceTransferJob &job, std::shared_ptr<const application::model::ResourceContentSnapshot> snapshot, application::model::ResourceDestination provider, std::string prefix, CloudPublisher publish, const application::model::ResourceTransferContext &context);
// 只接受本后端、同一资源及内容版本的受控对象意图，不接受 URL 或任意路径。
std::optional<std::string> cloudAttemptRelativePath(const application::model::ResourceTransferJob &job, std::string_view prefix);
// 用实测 hash 构造证据等级，核对旧对象默认不制造本次上传时间。
application::model::ResourceTransferReceipt cloudTransferReceipt(const application::model::ResourceTransferAttempt &attempt, std::string adapter, std::string algorithm, std::string value, std::string method);

} // namespace orchestrator::modules::crawler::infrastructure::storage::cloud
