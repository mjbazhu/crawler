#include "ResourceTargetVerificationSupport.h"

#include "../storage/rclone/RcloneTransferProtocol.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/ResourceContextHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"

#include <optional>

namespace orchestrator::modules::crawler::infrastructure::app::target_verification {
namespace rc = storage::rclone::transfer;
namespace {
// 从有界服务端配置中只选必要身份字段，不返回/持久化 pass、token 或原始响应。
std::optional<nlohmann::json> mountIdentity(const nlohmann::json &remote)
{
    if (!remote.is_object() || remote.value("type", "") != "webdav") return {};
    auto url = remote.value("url", ""), user = remote.value("user", "");
    if (user.empty() || user.size() > 512 || url.size() > 4096 || url.find_first_of("@?#\r\n") != std::string::npos || !http::client::detail::parseUrl(url)) return {};
    // bearer/custom headers 可改变实际认证身份，不能仍将 user 当作已核验账号。
    for (const auto key : {"bearer_token", "bearer_token_command", "headers"})
        if (remote.contains(key) && remote[key] != "") return {};
    while (!url.empty() && url.back() == '/') url.pop_back();
    return nlohmann::json {{"scope", "webdav_mount"}, {"url", url}, {"user", user}, {"vendor", remote.value("vendor", "other")}, {"layout", "resources/v1"}};
}
} // namespace

ResourceTargetVerification rcloneTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const application::model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> client)
{
    auto value = unavailable(target.provider, "rclone", "rclone_identity_unavailable");
    const auto settings = storage::rclone::detail::normalizeCloudConfig(target.rclone.value_or(config.rclone));
    if (target.rclone_client) client = target.rclone_client;
    if (!client) client = std::make_shared<http::client::AsioHttpClient>(makeHttpClientOptions(config));
    // 原始取消可能查询持久任务，RC 的 I/O 回调只读取监视器的原子快照。
    http::client::ResourceContextHttpClient monitor(client, context);
    const auto budget = monitor.executionContext();
    const auto colon = settings.target_fs.find(':');
    const auto name = settings.target_fs.substr(0, colon);
    const auto configured = rc::post(*client, settings, "config/get", {{"name", name}}, budget);
    if (!configured.isSuccess()) return value;
    if (configured.data()->is_object() && configured.data()->value("type", "") == "drive") {
        if (target.provider != application::model::ResourceDestination::Gdrive) return value;
        return driveTarget(config, target, context, std::move(client));
    }
    auto identity = mountIdentity(*configured.data());
    if (!identity) return value;
    const auto info = rc::post(*client, settings, "operations/fsinfo", {{"fs", settings.target_fs}}, budget);
    if (!info.isSuccess()) {
        value.result_code = "rclone_capability_unavailable";
        return value;
    }
    const auto &fs = *info.data();
    // 缓存中的后端不能冒充另一个已配置根；只读 stat 证明该账号可访问目录。
    const auto root = settings.target_fs.substr(colon + 1);
    if (!fs.contains("Root") || !fs["Root"].is_string() || fs["Root"] != root || !fs.contains("Features") || !fs["Features"].is_object() || fs["Features"].value("IsLocal", true)) return value;
    const auto stat = rc::post(*client, settings, "operations/stat", {{"fs", settings.target_fs}, {"remote", ""}}, budget);
    if (!stat.isSuccess() || !stat.data()->contains("item") || !(*stat.data())["item"].is_object()) return value;
    const auto &directory = (*stat.data())["item"];
    if (!directory.value("IsDir", false)) return value;
    const auto checked = rc::post(*client, settings, "config/get", {{"name", name}}, budget);
    if (!checked.isSuccess() || mountIdentity(*checked.data()) != identity || context.stopped()) {
        value.result_code = "target_identity_changed";
        return value;
    }
    (*identity)["root"] = root;
    // 部分 WebDAV 实现有真实对象 ID；普通 WebDAV 缺失时范围仍只是挂载。
    if (directory.contains("ID") && directory["ID"].is_string()) (*identity)["root_id"] = directory["ID"];
    value.profile.fingerprint = fingerprint(*identity);
    value.profile.available = true;
    value.identity_scope = "webdav_mount";
    value.result_code.clear();
    return value;
}
} // namespace orchestrator::modules::crawler::infrastructure::app::target_verification
