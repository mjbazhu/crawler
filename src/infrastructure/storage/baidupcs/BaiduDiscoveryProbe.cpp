#include "crawler/infrastructure/storage/baidupcs/BaiduDiscoveryProbe.h"
#include "BaiduTransferProtocol.h"
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {
namespace {
namespace model = application::model;
// 不可变实例；runner需线程安全，真实runner仅管理本次子进程。
class Probe final : public application::ports::ResourceDiscoveryProbe {
public:
    // 未提供runner时使用既有有界进程执行器。
    Probe(BaiduPcsGoConfig config, execution::process::CommandRunner runner, std::function<application::result::ServiceResult<std::string>(const model::ResourceTransferContext &)> identity) : config_(transfer::normalize(std::move(config))), runner_(runner ? std::move(runner) : execution::process::runCommand), identity_(std::move(identity))
    {
        if (!identity_) throw std::invalid_argument("discovery identity required");
    }
    // 共用真实账户/根身份围栏。
    application::result::ServiceResult<std::string> fingerprint(const model::ResourceTransferContext &context) override
    {
        return identity_(context);
    }
    // 只有明确不存在才报告missing，其余错误保持unknown。
    application::result::ServiceResult<model::ResourceObjectObservation> observe(std::string path, const model::ResourceTransferContext &context) override
    {
        using Result = application::result::ServiceResult<model::ResourceObjectObservation>;
        if (!model::safeResourceDiscoveryPath(path)) return Result::failure(application::result::ServiceErrorType::InvalidParam, "invalid_discovery_path");
        const auto meta = transfer::meta(config_, runner_, (config_.cloud_root == "/" ? "/" : config_.cloud_root + "/") + path, context);
        model::ResourceObjectObservation value;
        if (meta.state == transfer::MetaState::File) {
            value.state = "found";
            value.size_bytes = meta.size;
        } else if (meta.state == transfer::MetaState::Missing)
            value.state = "missing";
        else if (meta.state == transfer::MetaState::Directory)
            value.state = "directory";
        return Result::success(std::move(value));
    }

private:
    const BaiduPcsGoConfig config_;                                                                                         // 固定根及独立超时配置
    const execution::process::CommandRunner runner_;                                                                        // 有界线程安全进程边界
    const std::function<application::result::ServiceResult<std::string>(const model::ResourceTransferContext &)> identity_; // 只读身份核验
};
} // namespace
std::unique_ptr<application::ports::ResourceDiscoveryProbe> makeBaiduDiscoveryProbe(BaiduPcsGoConfig config, execution::process::CommandRunner runner, std::function<application::result::ServiceResult<std::string>(const application::model::ResourceTransferContext &)> identity)
{
    return std::make_unique<Probe>(std::move(config), std::move(runner), std::move(identity));
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
