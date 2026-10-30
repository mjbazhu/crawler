#include "ResourceTargetVerificationSupport.h"

#include "../storage/baidupcs/BaiduTransferProtocol.h"
#include "../storage/rclone/RcloneTransferProtocol.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace model = application::model;
namespace native = storage::baidupcs::transfer;
namespace {
// 仅接受完整的当前账号字段，零 UID、重复账号行和诊断文字不作为身份。
std::string account(const execution::process::ProcessResult &result)
{
    if (result.exit_code != 0 || result.timed_out || result.cancelled) return {};
    const std::regex expression(R"((?:^|\n)当前帐号 uid: ([1-9][0-9]*), 用户名: [^\r\n]*(?:\r?\n|$))");
    auto match = std::sregex_iterator(result.output.begin(), result.output.end(), expression);
    if (match == std::sregex_iterator {}) return {};
    const auto uid = (*match)[1].str();
    return ++match == std::sregex_iterator {} ? uid : std::string {};
}
// meta 的 fs_id 是远端对象身份；不把路径字符串当成根目录身份证据。
std::string directoryId(const execution::process::ProcessResult &result, const std::string &root)
{
    if (result.exit_code != 0 || result.cancelled || result.timed_out) return {};
    std::smatch match;
    const std::regex path(R"((?:^|\n)[ \t|]*目录路径[ \t|:：]+([^\r\n|]+))");
    if (!std::regex_search(result.output, match, path)) return {};
    auto actual = match[1].str();
    while (!actual.empty() && (actual.back() == ' ' || actual.back() == '\t')) actual.pop_back();
    if (actual != root) return {};
    const std::regex id(R"((?:^|\n)[ \t|]*fs_id[ \t|:]+([1-9][0-9]*)[ \t|]*(?:\r?\n|$))");
    return std::regex_search(result.output, match, id) ? match[1].str() : std::string {};
}
} // namespace

ResourceTargetVerification verifyCloudResourceTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> client, execution::process::CommandRunner runner)
{
    auto adapter = target.adapter.empty() ? config.resource_cloud_upload_backend : target.adapter;
    std::transform(adapter.begin(), adapter.end(), adapter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto value = target_verification::unavailable(target.provider, adapter, "target_identity_unavailable");
    if (context.stopped()) {
        value.result_code = "target_probe_stopped";
        return value;
    }
    if (!model::supportsResourceTransferAdapter(target.provider, adapter) || target.provider == model::ResourceDestination::Local) {
        value.result_code = "target_adapter_invalid";
        return value;
    }
    try {
        if (adapter == "baidupcs") {
            auto settings = config.baidupcs;
            settings.local_root = target.staging_root;
            settings = native::normalize(std::move(settings));
            if (!runner) runner = execution::process::runCommand;
            const auto before = account(native::run(settings, runner, {"who"}, context));
            if (before.empty()) return value;
            const auto root_id = directoryId(native::run(settings, runner, {"meta", settings.cloud_root}, context), settings.cloud_root);
            if (root_id.empty()) return value;
            const auto after = account(native::run(settings, runner, {"who"}, context));
            if (before != after || context.stopped()) {
                value.result_code = "target_identity_changed";
                return value;
            }
            value.profile.fingerprint = target_verification::fingerprint({{"provider", "baidu"}, {"account", before}, {"root_id", root_id}, {"root", settings.cloud_root}, {"layout", "resources/v1"}});
            value.profile.available = true;
            value.identity_scope = "provider_account";
            value.result_code.clear();
        } else {
            return target_verification::rcloneTarget(config, target, context, std::move(client));
        }
    } catch (const std::exception &) {}
    return value;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
