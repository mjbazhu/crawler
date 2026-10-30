#include "crawler/infrastructure/app/ResourceTargetVerifier.h"
#include "../../../src/infrastructure/app/ResourceTargetVerificationSupport.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"

#include <nlohmann/json.hpp>
#include <array>
#include <functional>
#include <future>
#include <iostream>

namespace c = orchestrator::modules::crawler;
namespace app = c::infrastructure::app;
namespace http = c::infrastructure::http::client;
namespace test = c::tests::cloud_transfer;
using Destination = c::application::model::ResourceDestination;
using Json = nlohmann::json;
namespace {
thread_local bool identity_io_callback = false; // 模拟 I/O 回调线程，禁止调用原始持久取消函数
// 固定 RC fixture，只允许只读探测；正文被消费但不会持久化认证。
class ProbeClient final : public http::HttpClient {
public:
    nlohmann::json config {{"type", "webdav"}, {"url", "https://dav.example.test/dav/"}, {"user", "fixture"}, {"pass", "fixture-only"}};
    std::string root = "Media";
    std::string root_id;
    std::string resolved_root_id;   // 精确父挂载 stat 返回的实际目录 ID
    std::function<void()> on_about; // 模拟后端真实请求触发 OAuth 刷新
    int calls = 0, config_calls = 0;
    int stat_calls = 0;
    bool directory = true, change = false, change_root = false;
    std::optional<http::HttpResponse> get(std::string_view, const std::vector<std::string> &) override
    {
        return {};
    }
    std::optional<http::HttpResponse> post(std::string_view, std::string_view, const std::vector<std::string> &) override
    {
        return {};
    }
    AwaitableResponse coGet(std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    }
    AwaitableResponse coPost(std::string, std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    }
    http::HttpUploadResult streamPostWithOptions(std::string_view url, http::HttpUploadBody input, const std::vector<std::string> &, const http::HttpRequestOptions &options) override
    {
        ++calls;
        test::require(options.request_timeout.count() > 0 && options.max_response_bytes > 0, "unbounded identity request");
        std::array<char, 1024> bytes {};
        std::string request_text;
        while (true) {
            const auto size = input.read(bytes);
            test::require(size.has_value(), "invalid probe body");
            if (!*size) break;
            request_text.append(bytes.data(), *size);
        }
        nlohmann::json body;
        if (url.ends_with("/config/get")) {
            body = config;
            if (change && ++config_calls > 1) body["user"] = "changed";
        } else if (url.ends_with("/operations/fsinfo"))
            body = {{"Root", root}, {"Features", {{"IsLocal", false}}}};
        else if (url.ends_with("/operations/stat")) {
            body = {{"item", {{"IsDir", directory}}}};
            if (!root_id.empty()) body["item"]["ID"] = change_root && ++stat_calls > 1 ? "changed-folder" : root_id;
            const auto request = Json::parse(request_text);
            if (!resolved_root_id.empty() && request.value("remote", "") == root && request.value("fs", "").ends_with(':')) body["item"]["ID"] = resolved_root_id;
        } else if (url.ends_with("/operations/about")) {
            if (on_about) on_about();
            body = {{"total", 1024}};
        } else
            throw std::runtime_error("identity probe performed a write");
        http::HttpResponse response;
        response.status_code = 200;
        const auto encoded = body.dump();
        response.body.assign(encoded.begin(), encoded.end());
        return {response, input.size_bytes, true, {}};
    }
};
// 官方 Google 身份协议替身；只能接收固定端点及 Bearer，不使用 RC Basic 认证。
class IdentityClient final : public http::HttpClient {
public:
    std::string account = "permission-123";      // 模拟稳定账号身份
    std::string bearer = "fixture-access";       // 仅测试内的 OAuth bearer
    std::string symbolic_root_id = "folder-123"; // Google 对 root 的真实解析
    int status = 200, account_calls = 0, file_calls = 0;
    bool changed_account = false, changed_root = false, folder = true, wrong_id = false;
    // 不允许测试静默退化到没有预算的 GET。
    std::optional<http::HttpResponse> get(std::string_view, const std::vector<std::string> &) override
    {
        throw std::runtime_error("unbounded identity GET");
    }
    // 校验真实代码的官方 URL、预算、独立认证和取消缓存。
    std::optional<http::HttpResponse> getWithOptions(std::string_view url, const std::vector<std::string> &headers, const http::HttpRequestOptions &options) override
    {
        test::require(options.request_timeout.count() > 0 && options.request_timeout.count() <= 10000 && options.max_response_bytes == 65536 && bool(options.cancelled), "unbounded Google identity request");
        test::require(headers == std::vector<std::string> {"Accept: application/json", "Authorization: Bearer " + bearer}, "Google request leaked RC credentials or changed bearer");
        auto callback = std::async(std::launch::async, [&] { identity_io_callback = true; return options.cancelled(); });
        test::require(!callback.get(), "active identity request cancelled unexpectedly");
        Json body;
        if (url == "https://www.googleapis.com/drive/v3/about?fields=user(permissionId)") {
            body = {{"user", {{"permissionId", changed_account && ++account_calls > 1 ? "changed-account" : account}}}};
        } else {
            constexpr std::string_view prefix = "https://www.googleapis.com/drive/v3/files/";
            constexpr std::string_view suffix = "?fields=id,mimeType,driveId&supportsAllDrives=true";
            test::require(url.starts_with(prefix) && url.ends_with(suffix), "Google identity used an arbitrary URL");
            const auto requested = std::string(url.substr(prefix.size(), url.size() - prefix.size() - suffix.size()));
            auto id = requested == "root" ? symbolic_root_id : requested;
            if (changed_root && ++file_calls > 1) id = "changed-folder";
            if (wrong_id) id = "wrong-folder";
            body = {{"id", id}, {"mimeType", folder ? "application/vnd.google-apps.folder" : "application/octet-stream"}};
        }
        http::HttpResponse response;
        response.status_code = status;
        const auto text = body.dump();
        response.body.assign(text.begin(), text.end());
        return response;
    }
    // 写入和协程路径均不属于身份核验。
    std::optional<http::HttpResponse> post(std::string_view, std::string_view, const std::vector<std::string> &) override
    {
        throw std::runtime_error("identity write attempted");
    }
    AwaitableResponse coGet(std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    }
    AwaitableResponse coPost(std::string, std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    }
};
} // namespace

int main()
{
    try {
        test::Root root;
        const auto local = app::verifyLocalResourceTarget(root.path, test::context());
        test::require(local.profile.available && local.identity_scope == "filesystem_object" && local.profile.fingerprint.size() == 64, "existing local identity missing");
        test::require(app::verifyLocalResourceTarget(root.path / ".", test::context()).profile.fingerprint == local.profile.fingerprint, "equivalent local roots differ");
        test::require(!app::verifyLocalResourceTarget(root.path / "absent", test::context()).profile.available && !std::filesystem::exists(root.path / "absent"), "probe created missing local root");
        app::ServerApplication::Config config;
        config.resource_cloud_upload_backend = "rclone";
        config.rclone.rc_url = "http://127.0.0.1:5572";
        config.rclone.target_fs = "archive:Media";
        app::ResourceCloudTarget target {Destination::Baidu, {}};
        auto client = std::make_shared<ProbeClient>();
        const auto observed = app::verifyCloudResourceTarget(config, target, test::context(), client);
        test::require(observed.profile.available && observed.identity_scope == "webdav_mount", "WebDAV mount not verified");
        client->config["pass"] = "rotated-fixture";
        config.rclone.target_fs = "another_alias:Media";
        target.provider = Destination::Gdrive;
        test::require(app::verifyCloudResourceTarget(config, target, test::context(), client).profile.fingerprint == observed.profile.fingerprint, "password, alias or declared platform altered physical mount identity");
        client->config["user"] = "other";
        test::require(app::verifyCloudResourceTarget(config, target, test::context(), client).profile.fingerprint != observed.profile.fingerprint, "changed WebDAV user reused identity");
        client->change = true;
        client->config_calls = 0;
        test::require(!app::verifyCloudResourceTarget(config, target, test::context(), client).profile.available, "mid-probe identity change accepted");
        client->change = false;
        client->directory = false;
        test::require(!app::verifyCloudResourceTarget(config, target, test::context(), client).profile.available, "file accepted as target root");
        client->directory = true;
        client->config["bearer_token"] = "fixture-only";
        test::require(!app::verifyCloudResourceTarget(config, target, test::context(), client).profile.available, "opaque auth guessed as username");
        const auto calls = client->calls;
        auto stopped = test::context();
        stopped.cancelled = [] { return true; };
        test::require(!app::verifyCloudResourceTarget(config, target, stopped, client).profile.available && calls == client->calls, "cancelled probe performed I/O");
        {
            app::ServerApplication::Config drive_config;
            drive_config.resource_cloud_upload_backend = "rclone";
            drive_config.outbound_http_verify_tls = false;
            drive_config.rclone.rc_url = "http://127.0.0.1:5572";
            drive_config.rclone.rc_user = "rc-basic-fixture";
            drive_config.rclone.rc_password = "rc-basic-fixture-only";
            drive_config.rclone.target_fs = "archive:Media";
            auto rc = std::make_shared<ProbeClient>();
            rc->root_id = "folder-123";
            rc->config = {{"type", "drive"}, {"token", Json {{"access_token", "fixture-access"}, {"token_type", "Bearer"}}.dump()}};
            auto google = std::make_shared<IdentityClient>();
            app::ResourceCloudTarget drive_target {Destination::Gdrive, {}, 8 * 1024 * 1024, "rclone"};
            auto budget = test::context();
            budget.cancelled = [] { test::require(!identity_io_callback, "Google I/O callback called persistent cancellation"); return false; };
            auto probe = [&] { return app::target_verification::driveTarget(drive_config, drive_target, budget, rc, google); };
            rc->config["token"] = Json {{"access_token", "fixture-expired"}, {"token_type", "Bearer"}}.dump();
            rc->on_about = [&] { rc->config["token"] = Json {{"access_token", "fixture-access"}, {"token_type", "Bearer"}}.dump(); };
            const auto drive = probe();
            test::require(drive.profile.available && drive.identity_scope == "provider_account" && drive.profile.fingerprint.size() == 64, "Drive account and actual folder identity unavailable");
            rc->on_about = {};
            rc->config["token"] = Json {{"access_token", "fixture-access-rotated"}, {"token_type", "Bearer"}}.dump();
            google->bearer = "fixture-access-rotated";
            drive_config.rclone.target_fs = "alias:Media";
            test::require(probe().profile.fingerprint == drive.profile.fingerprint, "token refresh or remote alias changed stable Drive identity");
            google->changed_account = true;
            google->account_calls = 0;
            test::require(!probe().profile.available, "Drive account changed during probe");
            google->changed_account = false;
            rc->change_root = true;
            rc->stat_calls = 0;
            test::require(!probe().profile.available, "Drive actual folder changed during probe");
            rc->change_root = false;
            google->wrong_id = true;
            test::require(!probe().profile.available, "Google metadata disagreed with RC root but was accepted");
            google->wrong_id = false;
            google->folder = false;
            test::require(!probe().profile.available, "Google ordinary file accepted as Drive root");
            google->folder = true;
            google->status = 302;
            test::require(!probe().profile.available, "Google redirect accepted as identity");
            google->status = 200;
            rc->root_id.clear();
            rc->resolved_root_id = "folder-123";
            test::require(probe().profile.fingerprint == drive.profile.fingerprint, "exact parent stat did not resolve synthetic mounted root");
            rc->resolved_root_id.clear();
            test::require(!probe().profile.available, "path string guessed as actual Drive folder ID");
            rc->root.clear();
            rc->root_id = "root";
            drive_config.rclone.target_fs = "archive:";
            test::require(probe().profile.fingerprint == drive.profile.fingerprint, "Google symbolic root did not resolve to stable actual ID");
            for (const auto key : {"root_folder_id", "team_drive"}) {
                rc->config[key] = "configured-other-root";
                test::require(!probe().profile.available, "configured Drive root guessed as personal My Drive");
                rc->config.erase(key);
            }
            google->symbolic_root_id = "root";
            test::require(!probe().profile.available, "symbolic root accepted as actual Drive ID");
            google->symbolic_root_id = "folder-123";
            for (const auto key : {"service_account_file", "service_account_credentials", "impersonate", "auth_url", "token_url", "token_command", "env_auth", "client_credentials"}) {
                rc->config[key] = "fixture-unsupported";
                test::require(!probe().profile.available, "unsupported Drive identity configuration accepted");
                rc->config.erase(key);
            }
            rc->config["token"] = Json {{"access_token", "fixture-access\r\nInjected: value"}}.dump();
            test::require(!probe().profile.available, "Drive bearer header injection accepted");
            const auto before_stopped = rc->calls;
            auto cancelled = budget;
            cancelled.cancelled = [] { return true; };
            test::require(!app::target_verification::driveTarget(drive_config, drive_target, cancelled, rc, google).profile.available && rc->calls == before_stopped, "cancelled Drive probe performed I/O");
            drive_target.provider = Destination::Baidu;
            test::require(!probe().profile.available && rc->calls == before_stopped, "native Drive mislabeled as Baidu");
        }
        config.resource_cloud_upload_backend = "baidupcs";
        config.baidupcs.executable_path = "fixture-pcs";
        target = {Destination::Baidu, root.path};
        int who = 0;
        bool switch_account = false;
        auto runner = [&](const std::string &command, int timeout, const std::function<bool()> &) {
            test::require(timeout > 0, "unbounded native probe");
            if (command.find("who") != std::string::npos) {
                const auto uid = ++who > 1 && switch_account ? "456" : "123";
                return c::infrastructure::execution::process::ProcessResult {0, false, false, "当前帐号 uid: " + std::string(uid) + ", 用户名: fixture, 性别: , 年龄: 0\n"};
            }
            test::require(command.find("meta") != std::string::npos, "native probe performed a write");
            return c::infrastructure::execution::process::ProcessResult {0, false, false, "类型 目录\n目录路径 /Media\nfs_id 789\n"};
        };
        const auto native = app::verifyCloudResourceTarget(config, target, test::context(), {}, runner);
        test::require(native.profile.available && native.identity_scope == "provider_account", "native identity not verified");
        who = 0;
        switch_account = true;
        test::require(!app::verifyCloudResourceTarget(config, target, test::context(), {}, runner).profile.available, "native account changed while probing");
        std::cout << "target identity: local, WebDAV, native Baidu, Google account/root, cancellation and identity changes passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
