#include "crawler/infrastructure/execution/process/ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::execution::process {

std::string quoteArgument(std::string_view argument)
{
    std::string quoted = "\"";
    std::size_t slashes = 0;
    for (char ch : argument) {
        if (ch == '\\') {
            ++slashes;
            continue;
        }
        quoted.append(ch == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        quoted += ch;
    }
    quoted.append(slashes * 2, '\\');
    return quoted + '"';
}

ProcessResult runCommand(const std::string &command, int timeout_ms, const std::function<bool()> &cancelled)
{
    ProcessResult result;
#ifdef _WIN32
    // 所有 Win32 句柄均独占持有，异常路径也会关闭 Job 并结束所创建的进程。
    const auto close = [](void *handle) { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); };
    using Handle = std::unique_ptr<void, decltype(close)>;
    SECURITY_ATTRIBUTES security {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_raw = nullptr, write_raw = nullptr;
    if (!CreatePipe(&read_raw, &write_raw, &security, 0)) return result;
    Handle read_pipe(read_raw, close), write_pipe(write_raw, close);
    if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0)) return result;
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr), close);
    if (input.get() == INVALID_HANDLE_VALUE) return result;
    Handle job(CreateJobObjectW(nullptr, nullptr), close);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return result;
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.c_str(), -1, nullptr, 0);
    if (size <= 1) return result;
    std::vector<wchar_t> wide(static_cast<std::size_t>(size));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.c_str(), -1, wide.data(), size)) return result;
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = startup.hStdError = write_pipe.get();
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) return result;
    Handle child(process.hProcess, close), thread(process.hThread, close);
    if (!AssignProcessToJobObject(job.get(), child.get())) {
        TerminateProcess(child.get(), 1);
        return result;
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        TerminateJobObject(job.get(), 1);
        return result;
    }
    write_pipe.reset();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((std::max)(1, timeout_ms));
    const auto drain = [&] {
        // 每次最多读取 64 KiB，避免持续输出使超时检查饥饿。
        for (int i = 0; i < 16; ++i) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(read_pipe.get(), nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[4096];
            if (!ReadFile(read_pipe.get(), buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &received, nullptr) || !received) break;
            if (result.output.size() < 65536) result.output.append(buffer, (std::min)(static_cast<std::size_t>(received), 65536 - result.output.size()));
        }
    };
    for (;;) {
        drain();
        if (WaitForSingleObject(child.get(), 25) == WAIT_OBJECT_0) break;
        result.cancelled = cancelled && cancelled();
        result.timed_out = std::chrono::steady_clock::now() >= deadline;
        if (result.cancelled || result.timed_out) {
            TerminateJobObject(job.get(), 1);
            WaitForSingleObject(child.get(), 5000);
            break;
        }
    }
    drain();
    DWORD code = 1;
    if (!result.cancelled && !result.timed_out && GetExitCodeProcess(child.get(), &code) && code != STILL_ACTIVE) result.exit_code = static_cast<int>(code);
#else
    (void)command;
    (void)timeout_ms;
    (void)cancelled;
#endif
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::execution::process
