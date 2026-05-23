#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"

#include <future>
#include <memory>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

// 任务执行辅助器，封装 TaskExecutor 提交、异常捕获和同步 fallback。
class TaskRunner {
public:
    // 保存执行器；executor 为空时 run 会在当前线程执行，submitDetached 会返回 false。
    explicit TaskRunner(
        std::shared_ptr<application::ports::TaskExecutor> executor = nullptr,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics = nullptr
    ) : executor_(std::move(executor)),
        diagnostics_(std::move(diagnostics))
    {}

    // 返回当前 runner 是否具备异步提交能力。
    bool configured() const
    {
        return static_cast<bool>(executor_);
    }

    // 提交一个带返回值任务；提交失败或执行异常时返回 fallback。
    template <typename T, typename F>
    std::future<T> submit(F task, T fallback, std::string context) const
    {
        auto promise = std::make_shared<std::promise<T>>();
        auto future = promise->get_future();

        if (!executor_) {
            promise->set_value(runSafely<T>(std::move(task), std::move(fallback), context, diagnostics_));
            return future;
        }

        T submit_failure = fallback;
        try {
            executor_->submit([promise, task = std::move(task), fallback = std::move(fallback), context = std::move(context), diagnostics = diagnostics_]() mutable {
                promise->set_value(runSafely<T>(std::move(task), std::move(fallback), context, diagnostics));
            });
        } catch (const std::exception &e) {
            logTaskFailure("submit " + context, e, diagnostics_);
            promise->set_value(std::move(submit_failure));
        } catch (...) {
            logUnknownTaskFailure("submit " + context, diagnostics_);
            promise->set_value(std::move(submit_failure));
        }

        return future;
    }

    // 执行一个带返回值任务；有 executor 时等待其完成。
    template <typename T, typename F>
    T run(F task, T fallback, std::string context) const
    {
        return submit<T>(std::move(task), std::move(fallback), std::move(context)).get();
    }

    // 提交一个无需结果的后台任务；没有 executor 或提交失败时返回 false。
    template <typename F>
    bool submitDetached(F task, std::string context) const
    {
        if (!executor_) {
            return false;
        }

        try {
            executor_->submit([task = std::move(task), context = std::move(context), diagnostics = diagnostics_]() mutable {
                try {
                    task();
                } catch (const std::exception &e) {
                    logTaskFailure(context, e, diagnostics);
                } catch (...) {
                    logUnknownTaskFailure(context, diagnostics);
                }
            });
            return true;
        } catch (const std::exception &e) {
            logTaskFailure("submit " + context, e, diagnostics_);
        } catch (...) {
            logUnknownTaskFailure("submit " + context, diagnostics_);
        }
        return false;
    }

private:
    // 记录已知异常，保留任务上下文。
    static void logTaskFailure(const std::string &context, const std::exception &error, const std::shared_ptr<application::ports::DiagnosticsSink> &diagnostics)
    {
        if (diagnostics) {
            diagnostics->error("TaskRunner", context + " failed: " + error.what());
        }
    }

    // 记录未知异常，避免任务静默失败。
    static void logUnknownTaskFailure(const std::string &context, const std::shared_ptr<application::ports::DiagnosticsSink> &diagnostics)
    {
        if (diagnostics) {
            diagnostics->error("TaskRunner", context + " failed with unknown error");
        }
    }

    // 在当前线程执行任务并把异常转换为 fallback。
    template <typename T, typename F>
    static T runSafely(F task, T fallback, const std::string &context, const std::shared_ptr<application::ports::DiagnosticsSink> &diagnostics)
    {
        try {
            return task();
        } catch (const std::exception &e) {
            logTaskFailure(context, e, diagnostics);
        } catch (...) {
            logUnknownTaskFailure(context, diagnostics);
        }
        return fallback;
    }

    std::shared_ptr<application::ports::TaskExecutor> executor_;      // 可选执行器
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics_; // 可选诊断输出
};

} // namespace orchestrator::modules::crawler::application::use_cases
