#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "mocks/MockMetadataParser.h"
#include "mocks/MockMusicProvider.h"
#include "mocks/MockSingerSourceRepository.h"

#include <chrono>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;

void test_singer_index_ingestion_flow()
{
    std::cout << "Running Migu singer index ingestion flow test..." << std::endl;

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    auto parser = std::make_shared<crawler::tests::MockMetadataParser>();
    auto singer_source_repository = std::make_shared<crawler::tests::MockSingerSourceRepository>();
    auto executor = std::make_shared<crawler::infrastructure::execution::threading::ThreadPool>(1);

    crawler::application::model::ProviderResponse response;
    response.body = {'s', 'i', 'n', 'g', 'e', 'r', 's'};
    provider->mock_singer_index_response = response;

    crawler::application::model::SingerSourceMetadata metadata;
    metadata.source = "migu";
    metadata.name = "周杰伦";
    metadata.normalized_name = "周杰伦";
    metadata.gender = crawler::domain::Gender::Male;
    metadata.source_item_key = "singer:112";
    metadata.source_singer_id = "112";
    metadata.source_country = "华语";
    parser->mock_singer_sources_by_body["singers"] = {metadata};

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

    const bool saved = service.crawlSingerIndexAndSaveSync("huayu-nan", 20);
    assert(saved);
    assert(provider->last_singer_index_tab == "huayu-nan");
    assert(singer_source_repository->saved_singers.size() == 1);
    assert(singer_source_repository->saved_singers.front().source_singer_id == "112");
    assert(singer_source_repository->saved_singers.front().gender == crawler::domain::Gender::Male);

    std::cout << "Migu singer index ingestion flow test passed." << std::endl;
}

int main()
{
    test_singer_index_ingestion_flow();
    return 0;
}
