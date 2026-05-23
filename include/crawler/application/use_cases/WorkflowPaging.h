#pragma once

#include "crawler/application/use_cases/TaskRunner.h"

#include <algorithm>
#include <future>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

constexpr int kWorkflowDefaultLimit = 20;
constexpr int kWorkflowSearchRows = 20;
constexpr int kWorkflowMaxPages = 50;

// 归一化业务 limit，避免无限制分页。
inline int normalizeWorkflowLimit(int limit)
{
    if (limit <= 0) {
        return kWorkflowDefaultLimit;
    }
    return (std::min)(limit, 500);
}

// 判断是否已达到调用方请求数量。
inline bool reachedWorkflowLimit(std::size_t size, int limit)
{
    return limit > 0 && size >= static_cast<std::size_t>(limit);
}

// 计算并发分页窗口大小。
inline int workflowPageWindow(int max_page_concurrency)
{
    return (std::max)(1, max_page_concurrency);
}

// 收集一个连续分页窗口，并按 Result::page 排序返回。
template <typename Result, typename Collector>
std::vector<Result> collectOrderedPageWindow(TaskRunner &page_runner, int first_page, int last_page, Collector collector, std::string context)
{
    std::vector<std::future<Result>> futures;
    futures.reserve(static_cast<std::size_t>(last_page - first_page + 1));

    for (int page = first_page; page <= last_page; ++page) {
        Result fallback;
        fallback.page = page;
        futures.push_back(page_runner.submit<Result>(
            [collector, page]() mutable {
                return collector(page);
            },
            std::move(fallback),
            context
        ));
    }

    std::vector<Result> pages;
    pages.reserve(futures.size());
    for (auto &future : futures) {
        pages.push_back(future.get());
    }

    std::sort(pages.begin(), pages.end(), [](const Result &left, const Result &right) {
        return left.page < right.page;
    });
    return pages;
}

} // namespace orchestrator::modules::crawler::application::use_cases
