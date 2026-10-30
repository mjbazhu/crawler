#include "crawler/infrastructure/app/CloudFileStoreComposition.h"
#include "crawler/infrastructure/app/ResourceTransferFactory.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduCloudFileStore.h"
#include "crawler/infrastructure/storage/rclone/RcloneCloudFileStore.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
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

        // 工厂同时装配双端；百度阻塞时 Google 仍可完成，且关闭 local 不接触正式根。
        request.relative_path = "migu/singers/1/cover/a.png";
        config.storage_path = (root.path / "disabled-formal-root").string();
        auto independent_http = std::make_shared<RcClient>();
        std::promise<void> baidu_entered, release_baidu;
        auto entered = baidu_entered.get_future();
        auto release = release_baidu.get_future().share();
        std::atomic<bool> announced = false;
        process::CommandRunner stalled_baidu = [&](const std::string &, int, const std::function<bool()> &) {
            if (!announced.exchange(true)) baidu_entered.set_value();
            release.wait();
            process::ProcessResult result;
            result.exit_code = -1;
            return result;
        };
        const std::vector<c::infrastructure::app::ResourceCloudTarget> targets {{model::ResourceDestination::Baidu, root.path / "baidu-stage", 8ULL * 1024 * 1024 * 1024, "baidupcs"}, {model::ResourceDestination::Gdrive, root.path / "google-stage", 8ULL * 1024 * 1024 * 1024, "rclone"}};
        auto dual = c::infrastructure::app::ResourceTransferFactory(config).build(false, targets, independent_http, stalled_baidu);
        test::require(!dual.local && dual.clouds.size() == 2 && dual.cloud(model::ResourceDestination::Baidu) && dual.cloud(model::ResourceDestination::Gdrive), "factory omitted one independent cloud destination");
        test::require(!std::filesystem::exists(config.storage_path) && !std::filesystem::exists(root.path / "baidu-stage"), "factory opened disabled local or staging before a transfer");
        auto baidu_attempt = std::async(std::launch::async, [&] { return dual.cloud(model::ResourceDestination::Baidu)->upload(request); });
        const bool started = entered.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        auto google_attempt = std::async(std::launch::async, [&] { return dual.cloud(model::ResourceDestination::Gdrive)->upload(request); });
        const bool finished_independently = google_attempt.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        release_baidu.set_value();
        const auto google_result = google_attempt.get();
        const auto baidu_result = baidu_attempt.get();
        test::require(started && finished_independently && google_result.ok && !baidu_result.ok, "blocked Baidu prevented Google completion");
        test::require(!std::filesystem::exists(config.storage_path), "cloud-only upload created formal local root");
        config.baidupcs.executable_path.clear();
        auto isolated_error = c::infrastructure::app::ResourceTransferFactory(config).build(true, targets, independent_http);
        test::require(isolated_error.local && !isolated_error.cloud(model::ResourceDestination::Baidu) && isolated_error.cloud(model::ResourceDestination::Gdrive) && !isolated_error.clouds.front().result_code.empty(), "one cloud configuration error discarded the other targets");
        test::require(!std::filesystem::exists(config.storage_path), "local assembly created its root before admission");
        model::SongResourceBlobStreamSaveRequest local_request;
        local_request.resource_id = 1;
        local_request.claim_generation = 1;
        local_request.require_database_ack = true;
        local_request.relative_path_hint = "migu/songs/factory/audio/PQ.bin";
        auto writer = isolated_error.local->beginSongResourceBlobWrite(local_request);
        std::string write_error;
        test::require(writer && writer->write("ab", write_error) && writer->write("c", write_error), "factory local could not open a lazy stream");
        const auto complete = writer->commit();
        test::require(complete.ok && complete.size_bytes == 3 && complete.checksum == cloud::sha256("abc"), "factory local did not commit complete streamed content");
        writer->accept();
        writer.reset();
        auto rejected = isolated_error.local->beginSongResourceBlobWrite(local_request);
        test::require(rejected && rejected->write("xyz", write_error), "second exclusive local attempt could not start");
        const auto unconfirmed = rejected->commit();
        test::require(unconfirmed.ok && unconfirmed.local_path != complete.local_path, "local attempts overwrote the same physical object");
        rejected->abort();
        rejected.reset();
        test::require(isolated_error.local->exists(complete.local_path) && !isolated_error.local->exists(unconfirmed.local_path), "rejected local attempt removed the confirmed file");
        auto disabled = c::infrastructure::app::ResourceTransferFactory(config).build(false, {}, independent_http);
        test::require(!disabled.local && disabled.clouds.empty(), "all-disabled factory constructed a destination");
        std::cout << "PASS: common config switch, Baidu readback, Google multipart/hash, reuse and unknown/mismatch boundaries\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
