#include "BaiduTransferProtocol.h"

#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <algorithm>
#include <charconv>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer {
namespace {
// 表格可带竖线，字段值不能包含其行尾填充。
std::string trimmed(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r|:");
    if (first == std::string::npos) return {};
    value.erase(0, first);
    if (value.starts_with("：")) value.erase(0, std::string("：").size());
    const auto last = value.find_last_not_of(" \t\r|");
    return last == std::string::npos ? std::string {} : value.substr(0, last + 1);
}
// 只解析整个字段名，不匹配诊断正文或被截断的路径。
std::string field(const std::string &text, std::string_view key)
{
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trimmed(std::move(line));
        if (!line.starts_with(key)) continue;
        auto value = line.substr(key.size());
        if (value.empty() || (value.front() != ' ' && value.front() != '\t' && value.front() != '|' && value.front() != ':' && !value.starts_with("："))) continue;
        return trimmed(std::move(value));
    }
    return {};
}
} // namespace

BaiduPcsGoConfig normalize(BaiduPcsGoConfig config)
{
    std::replace(config.cloud_root.begin(), config.cloud_root.end(), '\\', '/');
    if (config.cloud_root.empty()) config.cloud_root = "/Media";
    if (config.cloud_root.front() != '/') config.cloud_root.insert(config.cloud_root.begin(), '/');
    while (config.cloud_root.size() > 1 && config.cloud_root.back() == '/') config.cloud_root.pop_back();
    if (config.executable_path.empty() || config.local_root.empty() || (config.cloud_root != "/" && !cloud::safeRelativePath(std::string_view(config.cloud_root).substr(1)))) throw std::invalid_argument("invalid Baidu resource transfer configuration");
    return config;
}

std::string pathUtf8(const std::filesystem::path &path)
{
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

process::ProcessResult run(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, const std::vector<std::string> &arguments, const model::ResourceTransferContext &context, std::function<bool()> extra_stop)
{
    if (context.stopped()) return {};
    auto command = process::quoteArgument(config.executable_path);
    for (const auto &argument : arguments) command += ' ' + process::quoteArgument(argument);
    return runner(command, context.remaining(), [context, extra_stop = std::move(extra_stop)] { return context.stopped() || (extra_stop && extra_stop()); });
}

Meta meta(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, const std::string &path, const model::ResourceTransferContext &context)
{
    const auto result = run(config, runner, {"meta", path}, context);
    if (result.cancelled || result.timed_out || context.stopped()) return {};
    if (result.output.find("未登录") != std::string::npos || result.output.find("not logged") != std::string::npos) return {MetaState::Authentication, 0};
    if (std::regex_search(result.output, std::regex(R"((错误代码|错误码|代码|error_code|errno|errcode)[ \t"']*(:|：|=)[ \t"']*31066([^0-9]|$))", std::regex::icase))) return {MetaState::Missing, 0};
    if (result.exit_code != 0) return {};
    auto type = field(result.output, "类型");
    if (type.empty()) type = field(result.output, "文件类型");
    if ((type == "目录" || type == "文件夹") && field(result.output, "目录路径") == path) return {MetaState::Directory, 0};
    if (type != "文件" || field(result.output, "文件路径") != path) return {};
    const auto size = field(result.output, "文件大小");
    const auto end = size.find_first_not_of("0123456789");
    if (size.empty() || end == 0 || (end != std::string::npos && size[end] != ',' && size[end] != ' ' && size[end] != '\t')) return {};
    std::uint64_t parsed = 0;
    const auto limit = end == std::string::npos ? size.size() : end;
    const auto converted = std::from_chars(size.data(), size.data() + limit, parsed);
    if (converted.ec != std::errc {} || converted.ptr != size.data() + limit) return {};
    return {MetaState::File, parsed};
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer
