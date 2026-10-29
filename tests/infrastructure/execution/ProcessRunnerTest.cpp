#include "crawler/infrastructure/execution/process/ProcessRunner.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace process = orchestrator::modules::crawler::infrastructure::execution::process;

// 启动自身作为有界子进程，实际验证 Windows 引用、管道限长及 Job 超时终止。
int main(int argc, char *argv[])
{
    if (argc > 1) {
        const std::string mode = argv[1];
        if (mode == "echo") {
            std::cout << argv[2];
            return 7;
        }
        if (mode == "flood") {
            std::cout << std::string(200000, 'x');
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
        return 0;
    }
#ifdef _WIN32
    try {
        const auto require = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
        const auto executable = process::quoteArgument(argv[0]);
        const std::string argument = "path\\with space\\\"quote\\";
        auto echo = process::runCommand(executable + " echo " + process::quoteArgument(argument), 5000, {});
        require(echo.exit_code == 7 && echo.output == argument, "process quoting or exit status failed");
        auto flood = process::runCommand(executable + " flood", 5000, {});
        require(flood.exit_code == 0 && flood.output.size() == 65536, "bounded process output failed");
        const auto start = std::chrono::steady_clock::now();
        auto timeout = process::runCommand(executable + " sleep", 100, {});
        require(timeout.timed_out && timeout.exit_code == -1 && std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "process timeout did not terminate child");
        auto cancelled = process::runCommand(executable + " sleep", 5000, [] { return true; });
        require(cancelled.cancelled && cancelled.exit_code == -1, "process cancellation was ignored");
        std::cout << "PASS: native process quotes, exit status, output cap, timeout and cancellation\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
#endif
    return 0;
}
