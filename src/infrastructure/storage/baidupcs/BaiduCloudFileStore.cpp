#include "crawler/infrastructure/storage/baidupcs/BaiduCloudFileStore.h"

#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoUploader.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {
namespace {
namespace model = application::model;
namespace process = execution::process;

// 命令参数由固定操作、已校验路径及只读配置构造，认证仍由客户端自身管理。
std::string command(const std::string &executable, const std::vector<std::string> &arguments)
{
    std::string line = process::quoteArgument(executable);
    for (const auto &argument : arguments) line += ' ' + process::quoteArgument(argument);
    return line;
}
} // namespace

BaiduCloudFileStore::BaiduCloudFileStore(BaiduPcsGoConfig config, process::CommandRunner runner) : config_(std::move(config)), runner_(std::move(runner))
{
    std::replace(config_.cloud_root.begin(), config_.cloud_root.end(), '\\', '/');
    if (config_.cloud_root.empty()) config_.cloud_root = "/Media";
    if (config_.cloud_root.front() != '/') config_.cloud_root.insert(config_.cloud_root.begin(), '/');
    while (config_.cloud_root.size() > 1 && config_.cloud_root.back() == '/') config_.cloud_root.pop_back();
    if (config_.executable_path.empty() || config_.local_root.empty() || (config_.cloud_root != "/" && !cloud::safeRelativePath(std::string_view(config_.cloud_root).substr(1)))) throw std::invalid_argument("invalid Baidu cloud file configuration");
    config_.enabled = true;
    if (!runner_) runner_ = process::runCommand;
}

std::string BaiduCloudFileStore::backend() const
{
    return "baidupcs";
}
std::string BaiduCloudFileStore::prefix() const
{
    return config_.cloud_root == "/" ? "/" : config_.cloud_root + '/';
}

model::CloudFileResult BaiduCloudFileStore::upload(model::CloudFileUploadRequest request)
{
    if (!cloud::validUpload(request)) return cloud::failure("invalid_cloud_snapshot", "invalid_target");
    cloud::Deadline deadline(request.timeout_ms, request.cancelled);
    cloud::FileSnapshot snapshot(config_.local_root, request.relative_path, request.bytes);
    const auto path = prefix() + request.relative_path;
    const auto run = [&](const std::vector<std::string> &arguments) {
        return runner_(command(config_.executable_path, arguments), deadline.remaining(), request.cancelled);
    };
    // -1 未知、0 明确不存在、1 明确文件、2 目录、3 未登录。格式无法识别时不假定不存在。
    const auto probe = [&]() {
        if (!deadline.remaining()) return -1;
        const auto result = run({"meta", path});
        const auto &output = result.output;
        if (result.exit_code == 0 && output.find(path) != std::string::npos) {
            if (std::regex_search(output, std::regex("(文件类型|类型)[ \\t]+(目录|文件夹)|是否目录[ \\t]+(是|true)"))) return 2;
            if (std::regex_search(output, std::regex("(文件类型|类型)[ \\t]+(文件|普通文件)|是否目录[ \\t]+(否|false)"))) return 1;
        }
        if (output.find("未登录") != std::string::npos || output.find("not logged") != std::string::npos) return 3;
        // 只识别明确的错误码字段；路径、时间或普通诊断包含这些数字不代表缺失。
        if (std::regex_search(output, std::regex(R"((错误代码|错误码|代码|error_code|errno|errcode)[ \t"']*(:|：|=)[ \t"']*31066([^0-9]|$))", std::regex::icase))) return 0;
        return -1;
    };
    const auto stopped = [&] { return cloud::failure(deadline.cancelled() ? "cancelled" : "cloud_timeout", deadline.cancelled() ? "resource" : "timeout", true); };
    const int observed = probe();
    if (!deadline.remaining()) return stopped();
    if (observed == 3) return cloud::failure("cloud_auth_required", "invalid_target");
    if (observed == 2) return cloud::failure("cloud_target_is_directory", "invalid_target");
    if (observed < 0) return cloud::failure("baidupcs_verification_unknown", "network", true);
    const bool reused = observed == 1;
    if (!reused) {
        auto config = config_;
        config.local_root = snapshot.root();
        // 旧 uploader 只负责构造现有 upload 命令，实际执行统一走有界 runner。
        BaiduPcsGoUploader uploader(config, [&](const std::string &line) {
            if (!deadline.remaining()) return -1;
            return runner_(line, deadline.remaining(), request.cancelled).exit_code;
        });
        auto uploaded = uploader.upload({request.relative_path, request.bytes.size(), request.checksum, deadline.remaining()});
        if (!deadline.remaining()) return stopped();
        if (!uploaded.ok) return cloud::failure("baidupcs_upload_uncertain", "network", true);
        if (probe() != 1) return cloud::failure("baidupcs_verification_unknown", "network", true);
    }
    const auto verify_directory = snapshot.root() / "verified";
    std::filesystem::create_directory(verify_directory);
    if (!deadline.remaining()) return stopped();
    const auto downloaded = run({"download", "--saveto", verify_directory.generic_string(), "--retry", "0", path});
    if (!deadline.remaining()) return stopped();
    if (downloaded.exit_code != 0) return cloud::failure("baidupcs_readback_unknown", "network", true);
    const auto verified_file = verify_directory / std::filesystem::path(request.relative_path).filename();
    std::error_code error;
    if (!std::filesystem::is_regular_file(verified_file, error) || error || std::filesystem::file_size(verified_file, error) != request.bytes.size() || error) return cloud::failure("cloud_content_mismatch");
    std::ifstream input(verified_file, std::ios::binary);
    std::string bytes(request.bytes.size(), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof() || cloud::sha256(bytes) != request.checksum) return cloud::failure("cloud_content_mismatch");
    model::CloudFileResult result;
    result.ok = true;
    result.reused = reused;
    result.receipt = {backend(), path, request.bytes.size(), request.checksum};
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
