#pragma once

#include "crawler/application/ports/provider/RawPageFetcher.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/ParserDispatch.h"

#include <memory>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// URL 采集 workflow，只负责原始 URL 抓取、解析和保存。
class UrlIngestionWorkflow {
public:
    // 保存 URL 获取 port、解析调度器和持久化服务。
    UrlIngestionWorkflow(
        std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher,
        std::shared_ptr<ParserDispatch> parser,
        std::shared_ptr<MetadataPersistenceService> persistence
    );

    // 执行 URL 原始响应采集、解析和保存。
    bool processUrl(std::string url, int limit);

private:
    std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher_; // URL 原始响应 port
    std::shared_ptr<ParserDispatch> parser_;                               // 解析调度器
    std::shared_ptr<MetadataPersistenceService> persistence_;               // 持久化服务
};

} // namespace orchestrator::modules::crawler::application::use_cases
