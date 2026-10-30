#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace files = c::infrastructure::storage::filesystem;

namespace {
// 固定断言不输出清单正文。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 不完整或不安全的清单必须拒绝，不能通过负数包装资源身份。
void rejected(const std::string &text, bool page)
{
    bool failed = false;
    try {
        std::istringstream input(text);
        auto header = files::readResourceCloudManifestHeader(input, std::string(64, 'a'));
        if (page) (void)files::readResourceCloudManifestPage(input, header);
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    require(failed, "invalid manifest was accepted");
}
} // namespace

// 有界清单协议、源校验向量及危险数值的离线验证。
int main()
{
    const auto path = std::filesystem::temp_directory_path() / ("crawler-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jsonl");
    bool owns_path = false; // 失败清理只触及本次成功打开的临时文件
    try {
        files::ResourceCloudManifestHeader header;
        header.plan.run_id = "manifest-test";
        header.plan.operator_attested_both = true;
        header.scope.owner = model::ResourceOwner::Song;
        header.scope.database_identity = std::string(64, 'a');
        header.scope.high_water_id = 1;
        for (std::size_t i = 0; i < 3; ++i) header.scope.targets[i] = {static_cast<model::ResourceDestination>(i), 1, std::string(64, char('a' + i)), "maintenance"};
        auto page = header.scope;
        page.next_id = 1;
        model::LegacyResourceSnapshot row;
        row.identity = {model::ResourceOwner::Song, 1};
        row.source_identity = std::string(64, 'b');
        row.values_digest = std::string(64, 'c');
        row.status = 2;
        row.cloud_backend = "rclone";
        row.cloud_path = "/归档/对象";
        row.local_path = "missing/file";
        row.size_bytes = 123;
        row.checksum = "rclone:size:123";
        row.legacy_values = R"({"status":2,"cloud_backend":"rclone"})";
        page.rows.push_back(row);
        std::ostringstream output;
        files::writeResourceCloudManifestHeader(output, header);
        files::writeResourceCloudManifestPage(output, page);
        const auto text = output.str();
        std::istringstream input(text);
        const auto restored = files::readResourceCloudManifestHeader(input, std::string(64, 'd'));
        const auto data = files::readResourceCloudManifestPage(input, restored);
        require(restored.plan.manifest_hash == std::string(64, 'd') && restored.plan.operator_attested_both && restored.scope.rows.empty(), "header changed scope or attestation");
        require(data && data->rows.size() == 1 && data->rows.front().cloud_path == row.cloud_path && data->rows.front().checksum == row.checksum, "manifest lost UTF-8 metadata or raw checksum");
        require(!files::readResourceCloudManifestPage(input, restored), "EOF became a synthetic page");
        auto legacy = text;
        const std::string exclusion_field = "\"operator_excluded_all\":false,";
        legacy.erase(legacy.find(exclusion_field), exclusion_field.size());
        std::istringstream legacy_input(legacy);
        require(!files::readResourceCloudManifestHeader(legacy_input, std::string(64, 'd')).plan.operator_excluded_all, "old manifest acquired an exclusion policy");
        header.plan.operator_attested_both = false;
        header.plan.operator_excluded_all = true;
        std::ostringstream exclusion_output;
        files::writeResourceCloudManifestHeader(exclusion_output, header);
        std::istringstream exclusion_input(exclusion_output.str());
        const auto exclusion = files::readResourceCloudManifestHeader(exclusion_input, std::string(64, 'e'));
        require(exclusion.plan.operator_excluded_all && !exclusion.plan.operator_attested_both, "explicit exclusion became a dual-storage declaration");

        auto negative = text;
        const auto id = negative.find("\"resource_id\":1");
        require(id != std::string::npos, "fixture resource identity missing");
        negative.replace(id, std::string("\"resource_id\":1").size(), "\"resource_id\":-1");
        rejected(negative, true);
        auto duplicate = text;
        const auto destination = duplicate.find("\"destination\":\"baidu\"");
        require(destination != std::string::npos, "fixture destination missing");
        duplicate.replace(destination, std::string("\"destination\":\"baidu\"").size(), "\"destination\":\"local\"");
        rejected(duplicate, false);
        rejected("{}", false);
        rejected(std::string(16 * 1024 * 1024 + 1, 'x'), false);
        require(!std::filesystem::exists(path), "temporary checksum file already exists");
        {
            std::ofstream file(path, std::ios::binary);
            require(file.is_open(), "temporary checksum file could not be created");
            owns_path = true;
            file << "abc";
        }
        require(files::resourceCloudManifestSha256(path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "streaming SHA-256 disagrees with known vector");
        std::filesystem::remove(path);
        owns_path = false;
        std::cout << "PASS: bounded UTF-8 manifest pages, unsigned identities, unique targets and streaming digest\n";
        return 0;
    } catch (const std::exception &error) {
        std::error_code ignored;
        if (owns_path) std::filesystem::remove(path, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
