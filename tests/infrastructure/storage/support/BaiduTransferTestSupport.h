#pragma once

#include "crawler/infrastructure/execution/process/ProcessRunner.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"

#include <functional>
#include <map>
#include <regex>
#include <vector>

namespace orchestrator::modules::crawler::tests::cloud_transfer {
namespace process = infrastructure::execution::process;
// 仅操作测试目录的原生命令替身，不启动实际网盘客户端。
class BaiduRunner {
public:
    bool publish_before_barrier = false; // 模拟远端完成但响应尚未返回
    explicit BaiduRunner(std::filesystem::path remote) : root_(std::move(remote))
    {
        std::filesystem::create_directories(root_);
    } // 独占远端 fixture
    std::map<std::string, std::filesystem::path> objects;                            // 路径与真实临时文件
    bool unknown = false, directory = false, wrong_size = false, wrong_path = false; // 元信息边界
    bool wrong_bytes = false, oversize_readback = false, dropped_reply = false;      // 完整核对与未决响应
    int uploads = 0, downloads = 0, probes = 0;                                      // 实际命令计数
    std::function<void(const std::function<bool()> &)> on_upload;                    // 独立取消/并发屏障
    // 所有参数来自 quoteArgument，不接受任意 shell 文本。
    process::ProcessResult operator()(const std::string &command, int milliseconds, const std::function<bool()> &cancelled)
    {
        require(milliseconds > 0 && bool(cancelled), "Baidu command lacks independent budget");
        if (cancelled()) return {-1, false, true, {}};
        const std::regex quoted(R"arg("([^"]*)")arg");
        std::vector<std::string> args;
        for (auto it = std::sregex_iterator(command.begin(), command.end(), quoted); it != std::sregex_iterator(); ++it) args.push_back((*it)[1].str());
        require(args.size() >= 3 && args.front() == "pcs-fixture", "unexpected native command");
        require(std::none_of(args.begin(), args.end(), [](const auto &arg) { return arg == "config" || arg == "rm" || arg == "fixmd5"; }), "shared state or unrelated remote mutated");
        const auto &path = args.back();
        if (args[1] == "meta") {
            ++probes;
            if (unknown) return {1, false, false, "unrecognized transport failure 31066"};
            if (!objects.contains(path)) return {1, false, false, "错误码: 31066"};
            if (directory) return {0, false, false, "类型 目录\n目录路径 " + path + "\n"};
            const auto size = std::filesystem::file_size(objects.at(path)) + (wrong_size ? 1 : 0);
            return {0, false, false, "类型 文件\n文件路径 " + (wrong_path ? path + ".other" : path) + "\n文件大小 " + std::to_string(size) + ", fixture\nfs_id 11\n"};
        }
        if (args[1] == "upload") {
            require(command.find("\"--retry\" \"0\"") != std::string::npos && command.find("\"--policy\" \"skip\"") != std::string::npos && command.find("\"-l\" \"1\"") != std::string::npos, "Baidu upload bypassed frozen retry/exclusive policy");
            const auto source = std::filesystem::u8path(args[args.size() - 2]);
            const auto remote = path + '/' + utf8(source.filename());
            const auto store = [&] {
                const auto stored = root_ / (cloud::uniqueToken() + ".bin");
                std::filesystem::copy_file(source, stored);
                objects[remote] = stored;
                ++uploads;
            };
            if (publish_before_barrier) store();
            if (on_upload) on_upload(cancelled);
            if (cancelled()) return {-1, false, true, {}};
            if (!publish_before_barrier) store();
            return {dropped_reply ? 1 : 0, dropped_reply, false, {}};
        }
        require(args[1] == "download" && args[2] == "--saveto" && objects.contains(path), "unexpected readback command");
        ++downloads;
        const auto target = std::filesystem::u8path(args[3]) / std::filesystem::u8path(path.substr(path.find_last_of('/') + 1));
        if (wrong_bytes || oversize_readback)
            writeFile(target, std::filesystem::file_size(objects.at(path)) + (oversize_readback ? 131072 : 0), 'q');
        else
            std::filesystem::copy_file(objects.at(path), target);
        if (cancelled()) return {-1, false, true, {}};
        return {0, false, false, {}};
    }

private:
    std::filesystem::path root_; // 永不指向正式数据
};
} // namespace orchestrator::modules::crawler::tests::cloud_transfer
