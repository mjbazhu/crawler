#include "crawler/infrastructure/storage/baidupcs/BaiduPcsGoUploader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace baidupcs = crawler::infrastructure::storage::baidupcs;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path makeTempRoot()
{
    auto root = std::filesystem::temp_directory_path() / "crawler_baidupcs_uploader_test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path &path, const std::string &content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

void testUploadBuildsMediaPath()
{
    const auto root = makeTempRoot();
    const std::string relative_path = "migu/songs/600/audio/PQ.flac";
    writeFile(root / "migu" / "songs" / "600" / "audio" / "PQ.flac", "abc");

    std::vector<std::string> commands;
    baidupcs::BaiduPcsGoConfig config;
    config.enabled = true;
    config.executable_path = "D:/AI/code/BaiduPCS-Go/BaiduPCS-Go.exe";
    config.local_root = root;
    config.cloud_root = "/Media";
    config.upload_parallel = 48;
    config.upload_load = 16;
    config.upload_retries = 3;
    config.upload_policy = "overwrite";

    baidupcs::BaiduPcsGoUploader uploader(config, [&commands](const std::string &command) {
        commands.push_back(command);
        return 0;
    });

    model::SongResourceCloudUploadRequest request;
    request.local_path = "migu\\songs\\600\\audio\\PQ.flac";
    request.size_bytes = 3;
    request.checksum = "checksum";
    auto result = uploader.upload(std::move(request));

    require(result.ok, "upload should succeed with fake command runner");
    require(result.backend == "baidupcs", "backend should be baidupcs");
    require(result.cloud_path == "/Media/" + relative_path, "cloud path should stay under /Media");
    require(result.size_bytes == 3, "size should come from local file");
    require(result.checksum == "checksum", "checksum should be preserved");
    require(commands.size() == 1, "one command should be executed");
    require(commands.front().find("\"upload\"") != std::string::npos, "command should call upload");
    require(commands.front().find("\"/Media/migu/songs/600/audio\"") != std::string::npos, "command should target cloud parent directory");
    require(commands.front().find("\"-p\" \"48\"") != std::string::npos, "command should include upload parallel");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void testRejectsUnsafeRelativePath()
{
    const auto root = makeTempRoot();
    baidupcs::BaiduPcsGoConfig config;
    config.enabled = true;
    config.executable_path = "BaiduPCS-Go";
    config.local_root = root;

    baidupcs::BaiduPcsGoUploader uploader(config, [](const std::string &) {
        return 0;
    });

    model::SongResourceCloudUploadRequest request;
    request.local_path = "../escape.flac";
    auto result = uploader.upload(std::move(request));
    require(!result.ok, "unsafe relative path should fail");
    require(result.result_code == "invalid_local_path", "unsafe path should return invalid_local_path");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace

int main()
{
    try {
        testUploadBuildsMediaPath();
        testRejectsUnsafeRelativePath();
        std::cout << "BaiduPcsGoUploaderTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "BaiduPcsGoUploaderTest failed: " << e.what() << std::endl;
        return 1;
    }
}
