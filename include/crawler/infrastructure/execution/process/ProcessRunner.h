#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::execution::process {

// 有界子进程结果，不把外部命令原始输出直接暴露给 API。
struct ProcessResult {
    int exit_code = -1;     // 子进程返回码
    bool timed_out = false; // 已触发总超时终止
    bool cancelled = false; // 已触发协作取消
    std::string output;     // 有界 stdout/stderr，供 adapter 解析
};

// 进程测试边界；真实 runner 只管理本次创建的进程及其子进程。
using CommandRunner = std::function<ProcessResult(const std::string &, int, const std::function<bool()> &)>;

// 引用单个命令行参数，Windows 使用 CreateProcess 的转义规则。
std::string quoteArgument(std::string_view argument);
// Windows 无窗口执行，有界等待、取消和输出；其他平台明确返回不支持。
ProcessResult runCommand(const std::string &command, int timeout_ms, const std::function<bool()> &cancelled);

} // namespace orchestrator::modules::crawler::infrastructure::execution::process
