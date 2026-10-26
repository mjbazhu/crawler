#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoBatchUploader.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {

namespace {

using application::model::SongResourceBatchCloudUploadObjectResult;
using application::model::SongResourceBatchCloudUploadResult;

// 限制外部命令错误长度，避免污染 API 响应。
std::string trimMessage(std::string message)
{
    constexpr std::size_t kMaxMessageLength = 512;
    if (message.size() > kMaxMessageLength) {
        message.resize(kMaxMessageLength);
    }
    return message;
}

SongResourceBatchCloudUploadResult failure(std::string category, std::string code, std::string message)
{
    SongResourceBatchCloudUploadResult result;
    result.error_category = std::move(category);
    result.result_code = std::move(code);
    result.message = trimMessage(std::move(message));
    return result;
}

bool hasQuote(std::string_view value)
{
    return value.find('"') != std::string_view::npos;
}

std::string quoteArg(std::string_view value)
{
    return "\"" + std::string(value) + "\"";
}

std::string normalizeSlashes(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::vector<std::string> splitPath(std::string_view value)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto slash = value.find('/', start);
        const auto end = slash == std::string_view::npos ? value.size() : slash;
        parts.emplace_back(value.substr(start, end - start));
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

bool isSafeRelativePath(std::string_view value)
{
    if (value.empty() || value.front() == '/' || value.find(':') != std::string_view::npos) {
        return false;
    }
    for (const auto &part : splitPath(value)) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

std::string firstPathPart(std::string_view value)
{
    const auto slash = value.find('/');
    return std::string(value.substr(0, slash == std::string_view::npos ? value.size() : slash));
}

std::string normalizeCloudRoot(std::string root)
{
    root = normalizeSlashes(std::move(root));
    if (root.empty()) {
        root = "/Media";
    }
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    if (root.front() != '/') {
        root.insert(root.begin(), '/');
    }
    return root;
}

bool isSafeCloudRoot(const std::string &root)
{
    if (root.empty() || root.front() != '/' || root.find(':') != std::string::npos) {
        return false;
    }
    for (const auto &part : splitPath(std::string_view(root).substr(1))) {
        if (part == "." || part == "..") {
            return false;
        }
    }
    return true;
}

std::string joinCloudPath(const std::string &root, const std::string &relative_path)
{
    if (root == "/") {
        return "/" + relative_path;
    }
    return root + "/" + relative_path;
}

std::filesystem::path localFilePath(const std::filesystem::path &root, const std::string &relative_path)
{
    std::filesystem::path full = root;
    for (const auto &part : splitPath(relative_path)) {
        full /= part;
    }
    return full;
}

std::string safeBatchId(std::string value)
{
    if (value.empty()) {
        value = "batch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    for (char &ch : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_';
        if (!ok) {
            ch = '_';
        }
    }
    return value;
}

void removeTreeQuietly(const std::filesystem::path &path)
{
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

bool linkOrCopyFile(const std::filesystem::path &source, const std::filesystem::path &target, std::string &error)
{
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    std::filesystem::create_hard_link(source, target, ec);
    if (!ec) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) {
        return true;
    }
    error = ec.message();
    return false;
}

std::string buildCommand(
    const BaiduPcsGoConfig &config,
    const std::filesystem::path &local_path,
    const std::string &cloud_parent
)
{
    std::vector<std::string> args {
        config.executable_path,
        "upload",
        "-p",
        std::to_string((std::max)(1, config.upload_parallel)),
        "-l",
        std::to_string((std::max)(1, config.upload_load)),
        "--retry",
        std::to_string((std::max)(0, config.upload_retries)),
    };
    if (!config.upload_policy.empty()) {
        args.push_back("--policy");
        args.push_back(config.upload_policy);
    }
    args.push_back(local_path.generic_string());
    args.push_back(cloud_parent);

    std::ostringstream command;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            command << ' ';
        }
        command << quoteArg(args[index]);
    }
    return command.str();
}

int systemCommandRunner(const std::string &command)
{
#ifdef _WIN32
    if (command.empty()) {
        return 1;
    }
    const int required_size = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (required_size <= 0) {
        return 1;
    }
    std::wstring wide_command(static_cast<std::size_t>(required_size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wide_command.data(), required_size) <= 0) {
        return 1;
    }
    if (!wide_command.empty() && wide_command.back() == L'\0') {
        wide_command.pop_back();
    }
    if (wide_command.empty()) {
        return 1;
    }

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    std::vector<wchar_t> mutable_command(wide_command.begin(), wide_command.end());
    mutable_command.push_back(L'\0');

    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) {
        return static_cast<int>(GetLastError() == 0 ? 1 : GetLastError());
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        exit_code = GetLastError() == 0 ? 1 : GetLastError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
#else
    return std::system(command.c_str());
#endif
}

} // namespace

BaiduPcsGoBatchUploader::BaiduPcsGoBatchUploader(BaiduPcsGoConfig config, CommandRunner command_runner) : config_(std::move(config)),
                                                                                                           command_runner_(std::move(command_runner))
{
    if (!command_runner_) {
        command_runner_ = systemCommandRunner;
    }
}

application::model::SongResourceBatchCloudUploadResult BaiduPcsGoBatchUploader::uploadBatch(application::model::SongResourceBatchCloudUploadRequest request)
{
    if (!config_.enabled) {
        return failure("invalid_target", "cloud_upload_disabled", "BaiduPCS-Go batch cloud upload is disabled");
    }
    if (request.objects.empty()) {
        SongResourceBatchCloudUploadResult result;
        result.ok = true;
        result.backend = "baidupcs";
        result.result_code = "ok";
        return result;
    }
    if (config_.executable_path.empty() || config_.local_root.empty()) {
        return failure("invalid_target", "missing_dependency", "BaiduPCS-Go executable or local root is not configured");
    }
    if (hasQuote(config_.executable_path) || hasQuote(config_.upload_policy)) {
        return failure("invalid_target", "invalid_command_argument", "BaiduPCS-Go command argument contains an unsupported quote character");
    }

    const std::string cloud_root = normalizeCloudRoot(config_.cloud_root);
    if (!isSafeCloudRoot(cloud_root) || hasQuote(cloud_root)) {
        return failure("invalid_target", "invalid_cloud_root", "Baidu cloud root must be an absolute safe path");
    }

    const auto staging_root = config_.local_root / ".cloud-upload" / safeBatchId(std::move(request.batch_id));
    removeTreeQuietly(staging_root);

    std::set<std::string> top_dirs;
    std::vector<SongResourceBatchCloudUploadObjectResult> object_results;
    object_results.reserve(request.objects.size());
    for (const auto &object : request.objects) {
        const std::string relative_path = normalizeSlashes(object.local_path);
        if (!isSafeRelativePath(relative_path)) {
            removeTreeQuietly(staging_root);
            return failure("invalid_target", "invalid_local_path", "resource local path must be a safe relative path");
        }
        const auto source = localFilePath(config_.local_root, relative_path);
        std::error_code ec;
        if (!std::filesystem::exists(source, ec) || !std::filesystem::is_regular_file(source, ec)) {
            removeTreeQuietly(staging_root);
            return failure("persistence", "local_file_missing", "resource local file does not exist before BaiduPCS-Go batch upload");
        }
        if (object.size_bytes != 0 && static_cast<std::uint64_t>(std::filesystem::file_size(source, ec)) != object.size_bytes) {
            removeTreeQuietly(staging_root);
            return failure("persistence", "local_file_size_mismatch", "resource local file size does not match saved blob metadata");
        }

        const auto staged = localFilePath(staging_root, relative_path);
        std::string link_error;
        if (!linkOrCopyFile(source, staged, link_error)) {
            removeTreeQuietly(staging_root);
            return failure("persistence", "staging_file_failed", "failed to stage resource for BaiduPCS-Go batch upload: " + link_error);
        }

        top_dirs.insert(firstPathPart(relative_path));
        SongResourceBatchCloudUploadObjectResult item;
        item.resource_id = object.resource_id;
        item.local_path = relative_path;
        item.cloud_path = joinCloudPath(cloud_root, relative_path);
        object_results.push_back(std::move(item));
    }

    for (const auto &top_dir : top_dirs) {
        const auto upload_path = staging_root / top_dir;
        if (hasQuote(upload_path.string())) {
            removeTreeQuietly(staging_root);
            return failure("invalid_target", "invalid_command_argument", "BaiduPCS-Go upload path contains an unsupported quote character");
        }
        const std::string command = buildCommand(config_, upload_path, cloud_root);
        const int exit_code = command_runner_(command);
        if (exit_code != 0) {
            removeTreeQuietly(staging_root);
            return failure("persistence", "baidupcs_batch_upload_failed", "BaiduPCS-Go batch upload command failed with exit code " + std::to_string(exit_code));
        }
    }

    removeTreeQuietly(staging_root);
    SongResourceBatchCloudUploadResult result;
    result.ok = true;
    result.backend = "baidupcs";
    result.objects = std::move(object_results);
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
