#include "crawler/infrastructure/app/CloudFileStoreComposition.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduCloudFileStore.h"
#include "crawler/infrastructure/storage/rclone/RcloneCloudFileStore.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace cloud = c::infrastructure::storage::cloud;
namespace model = c::application::model;
namespace process = c::infrastructure::execution::process;

namespace {
// 模拟 RC 服务，不访问真实 Google，检查 multipart 和远端摘要协议。
class RcClient final : public c::infrastructure::http::client::HttpClient {
public:
    std::string bytes = "cloud image content"; // 预期完整内容
    bool present = false;                      // 云端对象存在性
    bool wrong_hash = false;                   // 模拟同大小错误内容
    bool unknown = false;                      // 模拟无效网络结果
    bool malformed = false;                    // 元信息字段类型不合法
    bool missing_hash = false;                 // 大小相同但缺少可靠摘要
    int uploads = 0;                           // 实际上传次数
    std::optional<c::infrastructure::http::client::HttpResponse> get(std::string_view, const std::vector<std::string> &) override
    {
        return std::nullopt;
    } // 不调用外部 GET
    AwaitableResponse coGet(std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    } // 不使用协程
    AwaitableResponse coPost(std::string, std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    } // 不使用协程
    // 只接受约定的 RC endpoint 与固定 Basic Auth。
    std::optional<c::infrastructure::http::client::HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers) override
    {
        if (unknown) return std::nullopt;
        test::require(std::find(headers.begin(), headers.end(), "Authorization: Basic dTpw") != headers.end(), "RC auth was not reused");
        nlohmann::json output;
        if (url.ends_with("operations/stat")) {
            const auto input = nlohmann::json::parse(body);
            test::require(input["fs"] == "gcloud:Media" && input["opt"]["showHash"] == true, "RC stat did not request file hashes");
            output["item"] = present ? nlohmann::json {{"Size", bytes.size()}, {"IsDir", false}, {"Hashes", {{"MD5", wrong_hash ? std::string(32, '0') : cloud::md5(bytes)}}}} : nlohmann::json(nullptr);
            if (present && malformed) output["item"]["IsDir"] = "false";
            if (present && missing_hash) output["item"]["Hashes"] = nlohmann::json::object();
        } else {
            test::require(url.find("operations/uploadfile?fs=gcloud%3AMedia&remote=migu%2Fsingers%2F1%2Fcover") != std::string_view::npos && body.find(bytes) != std::string_view::npos, "RC upload did not send the committed local snapshot");
            ++uploads;
            present = true;
            output = nlohmann::json::object();
        }
        const auto data = output.dump();
        return c::infrastructure::http::client::HttpResponse {200, std::vector<char>(data.begin(), data.end()), "application/json", {}};
    }
};
} // namespace

// 两个后端使用同一个 request 和回执协议，工厂开关也在本测试验证。
int main()
{
    try {
        test::TempRoot root;
        model::CloudFileUploadRequest request {"migu/singers/1/cover/a.png", "cloud image content", cloud::sha256("cloud image content"), 3000, {}};
        c::infrastructure::app::ServerApplication::Config config;
        config.storage_path = root.path.string();
        test::require(!c::infrastructure::app::makeCloudFileStore(config, nullptr), "off switch constructed a cloud uploader");
        config.resource_cloud_upload_enabled = true;
        config.resource_cloud_upload_backend = "rclone";
        config.rclone.rc_url = "http://rclone.local:5572";
        config.rclone.target_fs = "gcloud:Media";
        config.rclone.rc_user = "u";
        config.rclone.rc_password = "p";
        auto http = std::make_shared<RcClient>();
        auto google = c::infrastructure::app::makeCloudFileStore(config, http);
        auto uploaded = google->upload(request);
        test::require(uploaded.ok && uploaded.receipt.path == "gcloud:Media/" + request.relative_path && http->uploads == 1, "Google snapshot upload or cloud path failed");
        test::require(google->upload(request).reused && http->uploads == 1, "Google second pass uploaded again");
        http->wrong_hash = true;
        test::require(!google->upload(request).ok, "same-size wrong Google hash was accepted");
        http->wrong_hash = false;
        http->malformed = true;
        test::require(google->upload(request).uncertain, "malformed Google metadata was accepted or threw");
        http->malformed = false;
        http->missing_hash = true;
        test::require(google->upload(request).uncertain, "Google size alone was treated as verified content");
        http->missing_hash = false;
        http->unknown = true;
        test::require(google->upload(request).uncertain, "Google network failure was classified as absent");

        bool exists = false, wrong_bytes = false, unknown = false;
        int uploads = 0;
        process::CommandRunner runner = [&](const std::string &line, int timeout, const std::function<bool()> &) {
            test::require(timeout > 0 && timeout <= 3000, "Baidu runner did not receive bounded deadline");
            process::ProcessResult result;
            result.exit_code = 0;
            if (line.find("\"meta\"") != std::string::npos) {
                result.output = unknown ? "EOF: /Media/" + request.relative_path : (exists ? "文件路径 /Media/" + request.relative_path + "\n  类型              文件\n" : "代码: 31066");
            } else if (line.find("\"upload\"") != std::string::npos) {
                ++uploads;
                exists = true;
            } else {
                const std::string marker = "\"--saveto\" \"";
                const auto start = line.find(marker) + marker.size();
                const auto end = line.find('"', start);
                const auto path = std::filesystem::path(line.substr(start, end - start)) / "a.png";
                std::ofstream output(path, std::ios::binary);
                output << (wrong_bytes ? std::string(request.bytes.size(), 'x') : request.bytes);
            }
            return result;
        };
        config.resource_cloud_upload_backend = "baidupcs";
        config.baidupcs.executable_path = "fake-baidupcs.exe";
        config.baidupcs.cloud_root = "/Media";
        auto baidu = c::infrastructure::app::makeCloudFileStore(config, nullptr, runner);
        auto saved = baidu->upload(request);
        test::require(saved.ok && saved.receipt.checksum == request.checksum && uploads == 1, "Baidu did not confirm readback content");
        test::require(baidu->upload(request).reused && uploads == 1, "Baidu second pass uploaded again");
        // 文件路径或普通诊断带有错误码数字，不能据此判断不存在并盲目覆盖。
        request.relative_path = "migu/singers/31066/cover/a.png";
        test::require(baidu->upload(request).reused && uploads == 1, "Baidu treated a numeric path as a missing-file error");
        unknown = true;
        test::require(baidu->upload(request).uncertain && uploads == 1, "Baidu numeric-path verification failure caused blind upload");
        unknown = false;
        request.relative_path = "migu/singers/1/cover/a.png";
        wrong_bytes = true;
        test::require(!baidu->upload(request).ok, "same-size wrong Baidu content was accepted");
        unknown = true;
        test::require(baidu->upload(request).uncertain && uploads == 1, "Baidu EOF caused blind upload");
        request.relative_path = "../escape";
        test::require(!baidu->upload(request).ok && !google->upload(request).ok, "cloud adapters accepted unsafe relative path");
        std::cout << "PASS: common config switch, Baidu readback, Google multipart/hash, reuse and unknown/mismatch boundaries\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
