#include "crawler/infrastructure/storage/rclone/RcloneDiscoveryProbe.h"
#include "RcloneTransferProtocol.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::storage::rclone {
namespace {
namespace model = application::model;
using ObjectResult = application::result::ServiceResult<model::ResourceObjectObservation>;
// 仅支持stat；缺摘要时不会退回hashsumfile/download。
class Probe final : public application::ports::ResourceDiscoveryProbe {
public:
    // 不在构造期间访问RC或账户。
    Probe(RcloneConfig config, std::shared_ptr<http::client::HttpClient> client, std::function<application::result::ServiceResult<std::string>(const model::ResourceTransferContext &)> identity) : config_(detail::normalizeCloudConfig(std::move(config))), client_(std::move(client)), identity_(std::move(identity))
    {
        if (!client_ || !identity_) throw std::invalid_argument("discovery dependencies required");
    }
    // 使用与正式传输一致的目标身份围栏。
    application::result::ServiceResult<std::string> fingerprint(const model::ResourceTransferContext &context) override
    {
        return identity_(context);
    }
    // RPC只取精确对象元数据，HTTP/协议错误保持unknown。
    ObjectResult observe(std::string path, const model::ResourceTransferContext &context) override
    {
        if (!model::safeResourceDiscoveryPath(path)) return ObjectResult::failure(application::result::ServiceErrorType::InvalidParam, "invalid_discovery_path");
        const auto response = transfer::post(*client_, config_, "operations/stat", {{"fs", config_.target_fs}, {"remote", path}, {"opt", {{"showHash", true}}}}, context);
        model::ResourceObjectObservation value;
        if (!response.isSuccess()) return ObjectResult::success(value);
        const auto &body = *response.data();
        if (!body.contains("item")) return ObjectResult::success(value);
        const auto &item = body.at("item");
        if (item.is_null()) {
            value.state = "missing";
            return ObjectResult::success(value);
        }
        if (!item.is_object() || !item.contains("IsDir") || !item.at("IsDir").is_boolean()) return ObjectResult::success(value);
        if (item.at("IsDir").get<bool>()) {
            value.state = "directory";
            return ObjectResult::success(value);
        }
        if (!item.contains("Size") || !item.at("Size").is_number_integer() || (!item.at("Size").is_number_unsigned() && item.at("Size").get<std::int64_t>() < 0)) return ObjectResult::success(value);
        value.state = "found";
        value.size_bytes = item.at("Size").get<std::uint64_t>();
        if (item.contains("Hashes") && item.at("Hashes").is_object()) {
            for (const auto &[key, hash] : item.at("Hashes").items()) {
                if (!hash.is_string()) continue;
                auto name = key;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                name.erase(std::remove(name.begin(), name.end(), '-'), name.end());
                const auto normalized = model::resourceDiscoveryHash(name, hash.get<std::string>());
                if (!normalized.empty()) value.hashes[name] = normalized;
            }
        }
        return ObjectResult::success(std::move(value));
    }

private:
    const RcloneConfig config_;                                                                                             // 独立RC/fs及认证，只在请求内部使用
    const std::shared_ptr<http::client::HttpClient> client_;                                                                // 线程安全执行器
    const std::function<application::result::ServiceResult<std::string>(const model::ResourceTransferContext &)> identity_; // 只读身份检查
};
} // namespace
std::unique_ptr<application::ports::ResourceDiscoveryProbe> makeRcloneDiscoveryProbe(RcloneConfig config, std::shared_ptr<http::client::HttpClient> client, std::function<application::result::ServiceResult<std::string>(const application::model::ResourceTransferContext &)> identity)
{
    return std::make_unique<Probe>(std::move(config), std::move(client), std::move(identity));
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
