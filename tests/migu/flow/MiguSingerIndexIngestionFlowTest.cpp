#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "mocks/MockMetadataParser.h"
#include "mocks/MockMusicProvider.h"
#include "mocks/MockSingerSourceRepository.h"

#include <chrono>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;

// 故障断言抛异常，避免 Debug assert 触发系统弹窗。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_singer_index_ingestion_flow()
{
    std::cout << "Running Migu singer index ingestion flow test..." << std::endl;

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    auto parser = std::make_shared<crawler::tests::MockMetadataParser>();
    auto singer_source_repository = std::make_shared<crawler::tests::MockSingerSourceRepository>();
    auto executor = std::make_shared<crawler::infrastructure::execution::threading::ThreadPool>(1);

    crawler::application::model::ProviderResponse response;
    response.status_code = 503;
    response.body = {'s', 'i', 'n', 'g', 'e', 'r', 's'};
    provider->mock_singer_index_response = response;

    crawler::application::model::SingerSourceMetadata metadata;
    metadata.source = "migu";
    metadata.name = "周杰伦";
    metadata.normalized_name = "周杰伦";
    metadata.gender = crawler::domain::Gender::Male;
    metadata.source_singer_id = "112";
    metadata.source_country = "华语";
    auto second_metadata = metadata;
    second_metadata.name = "林俊杰";
    second_metadata.normalized_name = "林俊杰";
    second_metadata.source_singer_id = "113";
    parser->mock_singer_sources_by_body["singers"] = {metadata, second_metadata};

    crawler::application::use_cases::CrawlerService service(
        provider,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        parser,
        executor,
        executor,
        executor,
        executor,
        1,
        1,
        0,
        std::chrono::minutes(1),
        {},
        nullptr,
        singer_source_repository
    );

    require(!service.crawlSingerIndexAndSaveSync("huayu-nan", 0), "HTTP 503 index response was accepted as successful metadata");
    require(singer_source_repository->saved_singers.empty(), "failed index response reached persistence");
    response.status_code = 200;
    provider->mock_singer_index_response = response;
    const bool saved = service.crawlSingerIndexAndSaveSync("huayu-nan", 0);
    assert(saved);
    assert(provider->last_singer_index_tab == "huayu-nan");
    assert(singer_source_repository->saved_singers.size() == 2);
    assert(singer_source_repository->saved_singers.front().source_singer_id == "112");
    assert(singer_source_repository->saved_singers.front().gender == crawler::domain::Gender::Male);

    std::cout << "Migu singer index ingestion flow test passed." << std::endl;
}

int main()
{
    try {
        test_singer_index_ingestion_flow();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
