#include "ResourceTargetVerificationSupport.h"

#include "../storage/rclone/RcloneTransferProtocol.h"
#include "crawler/infrastructure/app/HttpClientOptionsFactory.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/ResourceContextHttpClient.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app::target_verification {
namespace rc = storage::rclone::transfer;
namespace {
using Json = nlohmann::json;
namespace model = application::model;
namespace net = http::client;
// Google 返回的稳定 ID 必须是有界路径段，不能注入 URL 或使用符号根。
bool stableId(std::string_view value)
{
    return !value.empty() && value != "root" && value != "0" && value.size() <= 512 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
    });
}
// 当前仅支持交互 OAuth；不把服务账号、外部凭据或可切换身份误作已登录账号。
std::optional<std::string> accessToken(const Json &remote)
{
    if (!remote.is_object() || remote.value("type", "") != "drive") return {};
    for (const auto key : {"service_account_file", "service_account_credentials", "impersonate", "auth_url", "token_url", "token_command"})
        if (remote.contains(key) && remote[key] != "") return {};
    for (const auto key : {"env_auth", "client_credentials"}) {
        if (!remote.contains(key)) continue;
        const auto &value = remote[key];
        if (value != "" && value != "false" && value != false) return {};
    }
    if (!remote.contains("token") || !remote["token"].is_string()) return {};
    const auto encoded = remote["token"].get<std::string>();
    if (encoded.size() > 65536) return {};
    const auto token = Json::parse(encoded, nullptr, false);
    if (!token.is_object() || token.value("token_type", "Bearer") != "Bearer" || !token.contains("access_token") || !token["access_token"].is_string()) return {};
    auto value = token["access_token"].get<std::string>();
    if (value.empty() || value.size() > 16384 || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || std::string_view("-._~+/=").find(static_cast<char>(ch)) != std::string_view::npos;
        })) return {};
    return value;
}
// URL 只能由本文件固定前缀和已验证的实际 ID 构成；不跟随重定向。
std::optional<Json> google(net::ResourceContextHttpClient &client, std::string url, const std::string &token, const model::ResourceTransferContext &context)
{
    if (context.stopped()) return {};
    net::HttpRequestOptions options {std::chrono::milliseconds((std::min)(10000, (std::max)(1, context.remaining()))), 65536};
    const auto response = client.getWithOptions(url, {"Accept: application/json", "Authorization: Bearer " + token}, options);
    if (!response || response->status_code != 200 || response->body.size() > options.max_response_bytes || context.stopped()) return {};
    const auto value = Json::parse(std::string_view(response->body.data(), response->body.size()), nullptr, false);
    return value.is_object() && !value.contains("error") ? std::optional<Json> {value} : std::nullopt;
}
// 账号来自 Google about，目录来自 RC 实测并由同一凭据向 Google 再核对。
std::optional<Json> observedIdentity(net::HttpClient &rc_client, net::ResourceContextHttpClient &identity_client, const storage::rclone::RcloneConfig &settings, const std::string &name, const model::ResourceTransferContext &context)
{
    const auto info = rc::post(rc_client, settings, "operations/fsinfo", {{"fs", settings.target_fs}}, context);
    if (!info.isSuccess()) return {};
    const auto root = settings.target_fs.substr(settings.target_fs.find(':') + 1);
    const auto &fs = *info.data();
    if (!fs.contains("Root") || !fs["Root"].is_string() || fs["Root"] != root || !fs.contains("Features") || !fs["Features"].is_object() || fs["Features"].value("IsLocal", true)) return {};
    const auto stat = rc::post(rc_client, settings, "operations/stat", {{"fs", settings.target_fs}, {"remote", ""}}, context);
    if (!stat.isSuccess() || !stat.data()->contains("item") || !(*stat.data())["item"].is_object()) return {};
    const auto &directory = (*stat.data())["item"];
    if (!directory.value("IsDir", false)) return {};
    auto id = directory.value("ID", "");
    // RC 的挂载根可能是合成条目；精确查询父挂载中的该目录以取得实际 ID。
    if (!root.empty() && (id.empty() || id == "root")) {
        const auto resolved = rc::post(rc_client, settings, "operations/stat", {{"fs", name + ':'}, {"remote", root}}, context);
        if (!resolved.isSuccess() || !resolved.data()->contains("item") || !(*resolved.data())["item"].is_object()) return {};
        const auto &actual = (*resolved.data())["item"];
        if (!actual.value("IsDir", false)) return {};
        id = actual.value("ID", "");
    }
    // 只有 remote 的空路径允许官方符号根解析，子路径缺失 ID 必须失败。
    const bool symbolic_root = root.empty() && (id.empty() || id == "root");
    if (!symbolic_root && !stableId(id)) return {};
    // fsinfo/stat 可能命中缓存；about 强制后端读取并触发正常 OAuth 刷新。
    const auto quota = rc::post(rc_client, settings, "operations/about", {{"fs", settings.target_fs}}, context);
    if (!quota.isSuccess()) return {};
    const auto configured = rc::post(rc_client, settings, "config/get", {{"name", name}}, context);
    if (!configured.isSuccess()) return {};
    // 配置自定义根或共享盘而缺少实际 ID 时，不能把个人 My Drive 当成它。
    if (symbolic_root && (!configured.data()->value("root_folder_id", "").empty() || !configured.data()->value("team_drive", "").empty())) return {};
    const auto token = accessToken(*configured.data());
    if (!token) return {};
    const auto about = google(identity_client, "https://www.googleapis.com/drive/v3/about?fields=user(permissionId)", *token, context);
    if (!about || !about->contains("user") || !(*about)["user"].is_object()) return {};
    const auto account = (*about)["user"].value("permissionId", "");
    if (!stableId(account)) return {};
    const auto file = google(identity_client, "https://www.googleapis.com/drive/v3/files/" + (symbolic_root ? "root" : id) + "?fields=id,mimeType,driveId&supportsAllDrives=true", *token, context);
    if (!file || file->value("mimeType", "") != "application/vnd.google-apps.folder") return {};
    const auto actual_id = file->value("id", "");
    if (!stableId(actual_id) || (!symbolic_root && actual_id != id)) return {};
    const auto drive_id = file->value("driveId", "");
    if (!drive_id.empty() && !stableId(drive_id)) return {};
    return Json {{"provider", "gdrive"}, {"account", account}, {"root_id", actual_id}, {"drive_id", drive_id}, {"layout", "resources/v1"}};
}
} // namespace

ResourceTargetVerification driveTarget(const ServerApplication::Config &config, const ResourceCloudTarget &target, const application::model::ResourceTransferContext &context, std::shared_ptr<http::client::HttpClient> rc_client, std::shared_ptr<http::client::HttpClient> identity_client)
{
    auto value = unavailable(target.provider, "rclone", "rclone_drive_identity_unavailable");
    if (target.provider != application::model::ResourceDestination::Gdrive || !rc_client || context.stopped()) return value;
    try {
        const auto settings = storage::rclone::detail::normalizeCloudConfig(target.rclone.value_or(config.rclone));
        if (!identity_client) {
            auto options = makeHttpClientOptions(config);
            options.verify_tls = true;
            options.io_threads = 1;
            options.request_timeout = std::chrono::seconds(10);
            options.max_response_bytes = 65536;
            identity_client = std::make_shared<http::client::AsioHttpClient>(options);
        }
        // 复用既有后台取消监视器，RC/Google 的 I/O 回调只读原子缓存。
        http::client::ResourceContextHttpClient observed(std::move(identity_client), context);
        const auto budget = observed.executionContext();
        const auto name = settings.target_fs.substr(0, settings.target_fs.find(':'));
        const auto before = observedIdentity(*rc_client, observed, settings, name, budget);
        if (!before) return value;
        const auto after = observedIdentity(*rc_client, observed, settings, name, budget);
        if (after != before || context.stopped()) {
            value.result_code = "target_identity_changed";
            return value;
        }
        value.profile.fingerprint = fingerprint(*before);
        value.profile.available = true;
        value.identity_scope = "provider_account";
        value.result_code.clear();
    } catch (const std::exception &) {}
    return value;
}
} // namespace orchestrator::modules::crawler::infrastructure::app::target_verification
