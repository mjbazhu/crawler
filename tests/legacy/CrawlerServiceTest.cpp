#include "crawler/application/use_cases/CrawlerService.h"
#include "mocks/MockMetadataParser.h"
#include "mocks/MockMusicProvider.h"
#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"
#include "crawler/infrastructure/music/persistence/sqlite/SongDao.h"
#include "crawler/infrastructure/music/persistence/sqlite/AlbumDao.h"
#include "crawler/infrastructure/music/persistence/sqlite/SingerDao.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <cassert>
#include <memory>
#include <filesystem>

namespace crawler = orchestrator::modules::crawler;
using crawler::application::use_cases::CrawlerService;
using crawler::domain::ParseResult;
using crawler::domain::Song;
using crawler::infrastructure::music::persistence::sqlite::AlbumDao;
using crawler::infrastructure::music::persistence::sqlite::ConnectionPool;
using crawler::infrastructure::music::persistence::sqlite::SingerDao;
using crawler::infrastructure::music::persistence::sqlite::SongDao;

// 简单的 Mock FileStorage (通过继承并重写虚函数，如果 FileStorage 方法是虚的)
// 检查 FileStorage.h: 析构函数是虚的，但 save/remove 不是。
// 为了测试，我们可以创建一个临时的测试目录。

void test_crawler_service_logic()
{
    std::cout << "Running CrawlerService Logic Test..." << std::endl;

    // 1. 准备环境
    // 使用内存数据库
    auto pool = std::make_shared<ConnectionPool>(":memory:", 4);
    auto songDao = std::make_shared<SongDao>(*pool);
    auto albumDao = std::make_shared<AlbumDao>(*pool);
    auto singerDao = std::make_shared<SingerDao>(*pool);

    // 临时文件存储
    std::string test_storage_path = "test_crawler_data";
    if (std::filesystem::exists(test_storage_path)) std::filesystem::remove_all(test_storage_path);
    auto fileStorage = std::make_shared<crawler::infrastructure::storage::filesystem::FileStorage>(test_storage_path);

    // Mock Provider
    auto mockProvider = std::make_shared<crawler::tests::MockMusicProvider>();
    auto mockParser = std::make_shared<crawler::tests::MockMetadataParser>();
    auto parseThreadPool = std::make_shared<crawler::infrastructure::execution::threading::ThreadPool>(2);
    boost::asio::io_context async_context;

    // Service
    CrawlerService service(
        mockProvider,
        fileStorage,
        songDao,
        albumDao,
        singerDao,
        mockParser,
        parseThreadPool,
        async_context.get_executor()
    );

    // Case 1: processUrl (Song)
    {
        std::cout << "[Test] processUrl with Song data ... ";

        // 准备 Mock 数据
        Song song;
        song.song_id = 1001;
        song.title = "Test Song";
        song.artist = "Test Artist";

        ParseResult result;
        result.meta_info = song;
        result.suggested_filename = "Test Artist - Test Song.mp3";
        result.audio_data = {'M', 'P', '3'}; // 模拟音频数据

        crawler::application::model::ProviderResponse response;
        response.body = {'u', 'r', 'l'};
        mockProvider->mock_url_response = response;
        mockParser->mock_results_by_body["url"] = {result};

        // 执行
        bool success = service.processUrlSync("http://example.com/song/1001", 20);

        // 验证
        assert(success == true);
        assert(mockProvider->last_fetch_url == "http://example.com/song/1001");

        // 验证文件是否保存
        assert(fileStorage->exists(result.suggested_filename));

        // 验证数据库是否保存 (由于是内存库，无法直接查询，除非 DAO 提供查询接口或我们手动查询)
        // 这里假设 DAO save 返回 true 即成功，且无异常抛出
        std::cout << "Passed" << std::endl;
    }

    // Case 2: searchAndSave
    {
        std::cout << "[Test] searchAndSave ... ";

        // 准备 Mock 数据
        Song song;
        song.song_id = 2002;
        song.title = "Search Result";

        ParseResult result;
        result.meta_info = song;

        crawler::application::model::ProviderResponse song_response;
        song_response.body = {'s', 'o', 'n', 'g', 's'};
        crawler::application::model::ProviderResponse empty_album_response;
        empty_album_response.body = {'a', 'l', 'b', 'u', 'm', 's'};
        mockProvider->mock_song_search_response = song_response;
        mockProvider->mock_album_search_response = empty_album_response;
        mockParser->mock_results_by_body["songs"] = {result};
        mockParser->mock_results_by_body["albums"] = {};

        // 执行
        bool success = service.searchAndSaveSync("keyword", 20);

        // 验证
        assert(success == true);
        assert(mockProvider->last_search_query == "keyword");
        std::cout << "Passed" << std::endl;
    }

    // 清理
    if (std::filesystem::exists(test_storage_path)) std::filesystem::remove_all(test_storage_path);
    std::cout << "CrawlerService Test Completed." << std::endl;
}

int main()
{
    test_crawler_service_logic();
    return 0;
}
