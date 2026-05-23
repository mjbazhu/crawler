#include "crawler/application/use_cases/SongResourcePathPlanner.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "mocks/MockHttpClient.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace use_cases = crawler::application::use_cases;
namespace http_client = crawler::infrastructure::http::client;
namespace filesystem_storage = crawler::infrastructure::storage::filesystem;

namespace {

// 测试断言失败时抛出异常，避免依赖 assert 宏影响 clangd 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 将文本转换为资源字节。
std::vector<char> bytes(std::string text)
{
    return std::vector<char>(text.begin(), text.end());
}

void testAudioPathPlanning()
{
    use_cases::SongResourcePathPlanner planner;
    model::SongResourcePathRequest request;
    request.source = "migu";
    request.source_content_id = "600902000006889266";
    request.source_song_id = "2643";
    request.resource_kind = "audio";
    request.format_type = "ZQ24";
    request.remote_url = "https://freetyst.nf.migu.cn/public/product.flac?Key=secret";

    const auto result = planner.plan(request);
    require(result.ok, "audio path should be planned");
    require(result.extension == "flac", "audio extension should come from url");
    require(result.relative_path == "migu/songs/600902000006889266/audio/ZQ24.flac", "audio relative path should be stable");
}

void testLyricPathPlanning()
{
    use_cases::SongResourcePathPlanner planner;
    model::SongResourcePathRequest request;
    request.source = "migu";
    request.source_content_id = "600902000006889266";
    request.resource_kind = "lyric";
    request.remote_url = "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y5";
    request.content_type = "text/plain; charset=utf-8";

    const auto result = planner.plan(request);
    require(result.ok, "lyric path should be planned");
    require(result.extension == "lrc", "lyric extension should use kind fallback");
    require(result.relative_path == "migu/songs/600902000006889266/lyrics/lyric.lrc", "lyric relative path should be song-level");
}

void testContentFetcher()
{
    auto client = std::make_shared<http_client::MockHttpClient>();
    http_client::HttpResponse response;
    response.status_code = 200;
    response.content_type = "audio/flac";
    response.body = bytes("music-bytes");
    client->mock_responses.emplace("https://example.test/song.flac", response);

    http_client::HttpSongResourceContentFetcher fetcher(client);
    model::SongResourceContentFetchRequest request;
    request.url = "https://example.test/song.flac";
    request.request_headers = {"User-Agent: test"};

    const auto result = fetcher.fetchSongResourceContent(request);
    require(result.ok, "content fetch should be ok");
    require(result.status_code == 200, "status code should be preserved");
    require(result.content_type == "audio/flac", "content type should be preserved");
    require(std::string(result.body.begin(), result.body.end()) == "music-bytes", "body should be preserved");
    require(client->last_request_headers.size() == 1, "headers should pass through");

    std::string streamed;
    model::SongResourceContentStreamCallbacks callbacks;
    callbacks.on_response = [](const model::SongResourceContentStreamInfo &info) {
        return info.status_code == 200 && info.content_type == "audio/flac";
    };
    callbacks.on_chunk = [&streamed](std::string_view chunk) {
        streamed.append(chunk.data(), chunk.size());
        return true;
    };
    const auto streamed_result = fetcher.streamSongResourceContent(request, callbacks);
    require(streamed_result.ok, "stream content fetch should be ok");
    require(streamed_result.bytes_received == 11, "stream byte count should be preserved");
    require(streamed == "music-bytes", "stream chunks should preserve body");

    request.url = "https://example.test/missing.flac";
    const auto missing = fetcher.fetchSongResourceContent(request);
    require(!missing.ok, "missing response should fail");
    require(missing.error_category == "network", "missing response should be network failure");
}

void testFileStorageSaveBlob()
{
    const auto root = std::filesystem::current_path() / "build" / "song-resource-storage-test";
    std::filesystem::remove_all(root);

    filesystem_storage::FileStorage storage(root);
    model::SongResourceBlobSaveRequest request;
    request.relative_path_hint = "migu/songs/600902000006889266/audio/ZQ24.flac";
    request.data = bytes("abc");
    request.content_type = "audio/flac";

    const auto result = storage.saveSongResourceBlob(request);
    require(result.ok, "blob save should be ok");
    require(result.local_path == request.relative_path_hint, "local path should use relative hint");
    require(result.size_bytes == 3, "size should be recorded");
    require(result.checksum == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 should be recorded");
    require(std::filesystem::exists(root / request.relative_path_hint), "saved file should exist");

    request.data = bytes("abcd");
    const auto overwritten = storage.saveSongResourceBlob(request);
    require(overwritten.ok, "same path overwrite should be ok");
    require(overwritten.size_bytes == 4, "overwritten size should be recorded");
    {
        std::ifstream saved_file(root / request.relative_path_hint, std::ios::binary);
        const std::string saved_text((std::istreambuf_iterator<char>(saved_file)), std::istreambuf_iterator<char>());
        require(saved_text == "abcd", "saved file should contain latest complete body");
    }

    request.relative_path_hint = "../escape.flac";
    const auto unsafe = storage.saveSongResourceBlob(request);
    require(!unsafe.ok, "unsafe path should be rejected");

    std::filesystem::remove_all(root);
}

void testFileStorageStreamSaveBlob()
{
    const auto root = std::filesystem::current_path() / "build" / "song-resource-storage-stream-test";
    std::filesystem::remove_all(root);

    filesystem_storage::FileStorage storage(root);
    model::SongResourceBlobStreamSaveRequest request;
    request.relative_path_hint = "migu/songs/600902000006889266/audio/ZQ24.flac";
    request.content_type = "audio/flac";

    auto writer = storage.beginSongResourceBlobWrite(request);
    require(static_cast<bool>(writer), "stream writer should open");
    std::string error;
    require(writer->write("ab", error), "first stream chunk should write");
    require(writer->write("cd", error), "second stream chunk should write");
    const auto result = writer->commit();

    require(result.ok, "stream blob save should be ok");
    require(result.local_path == request.relative_path_hint, "stream local path should use relative hint");
    require(result.size_bytes == 4, "stream size should be recorded");
    require(result.checksum == "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589", "stream sha256 should be recorded");
    {
        std::ifstream saved_file(root / request.relative_path_hint, std::ios::binary);
        const std::string saved_text((std::istreambuf_iterator<char>(saved_file)), std::istreambuf_iterator<char>());
        require(saved_text == "abcd", "stream saved file should contain all chunks");
    }

    request.relative_path_hint = "migu/songs/600902000006889266/audio/abort.flac";
    auto aborted = storage.beginSongResourceBlobWrite(request);
    require(static_cast<bool>(aborted), "abort stream writer should open");
    require(aborted->write("partial", error), "abort stream chunk should write");
    aborted->abort();
    require(!std::filesystem::exists(root / request.relative_path_hint), "aborted stream should not expose final file");

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    try {
        testAudioPathPlanning();
        testLyricPathPlanning();
        testContentFetcher();
        testFileStorageSaveBlob();
        testFileStorageStreamSaveBlob();
        std::cout << "SongResourceDownloadStorageTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "SongResourceDownloadStorageTest failed: " << e.what() << std::endl;
        return 1;
    }
}
