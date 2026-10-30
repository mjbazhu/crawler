#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace files = orchestrator::modules::crawler::infrastructure::storage::filesystem;
namespace model = orchestrator::modules::crawler::application::model;
namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 4> owners {"song", "singer", "album", "song_artifact"}; // 四类独立日志所有者
constexpr std::string_view marker = "crawler.resource-file-journal-probe.v1";                  // 防止恢复模式作用于真实文件根

// 断言错误只输出稳定分类，不打印文件内容或私有路径。
void require(bool value, const char *code)
{
    if (!value) throw std::runtime_error(code);
}

// 所有现存路径组件均拒绝链接与 Windows junction，隔离根不能越界。
void rejectLinks(const fs::path &path)
{
    auto current = fs::absolute(path).root_path();
    for (const auto &part : fs::absolute(path).relative_path()) {
        current /= part;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory) continue;
        require(!error, "path_inspection_failed");
        require(!fs::is_symlink(status), "linked_path_rejected");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) require((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "reparse_path_rejected");
#endif
    }
}

// 仅用于探针自己的新目录，禁止覆盖已有文件。
void writeNew(const fs::path &path, std::string_view content)
{
    require(!fs::exists(path), "existing_file_rejected");
    std::ofstream output(path, std::ios::binary);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    require(static_cast<bool>(output), "fixture_write_failed");
}

// 只读取有固定大小上界的探针元数据和短 fixture。
std::string readSmall(const fs::path &path)
{
    rejectLinks(path);
    require(fs::is_regular_file(path) && fs::file_size(path) < 16384, "fixture_file_invalid");
    std::ifstream input(path, std::ios::binary);
    const std::string result {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "fixture_read_failed");
    return result;
}

// 使用生产保存器生成实际 pending 日志，数据库与云端均不参与。
nlohmann::json seed(const fs::path &root)
{
    rejectLinks(root);
    require(!fs::exists(root), "seed_root_must_be_new");
    require(fs::create_directory(root), "seed_root_create_failed");
    writeNew(root / ".probe-owner", marker);
    files::FileStorage storage(root, false);
    nlohmann::json entries = nlohmann::json::array();
    for (std::size_t i = 0; i < owners.size(); ++i) {
        const auto owner = std::string(owners[i]);
        const auto previous = owner + "-previous.bin";
        writeNew(root / previous, "old-" + owner);
        model::SongResourceBlobStreamSaveRequest request;
        request.resource_id = i + 1;
        request.claim_generation = 7;
        request.owner_kind = owner;
        request.previous_local_path = previous;
        request.relative_path_hint = owner + "/object.bin";
        request.require_database_ack = true;
        auto session = storage.beginSongResourceBlobWrite(std::move(request));
        require(static_cast<bool>(session), "session_open_failed");
        std::string error;
        require(session->write("journal-fixture-" + owner, error), "session_write_failed");
        const auto committed = session->commit();
        require(committed.ok && !committed.local_path.empty(), "session_commit_failed");
        session->defer();
        entries.push_back({{"owner", owner}, {"id", i + 1}, {"path", committed.local_path}, {"previous", previous}, {"sha256", committed.checksum}, {"bytes", committed.size_bytes}});
    }
    writeNew(root / ".probe-entries.json", entries.dump());
    return {{"ok", true}, {"execution", "actual_filestorage_controlled_fixture"}, {"owners", 4}, {"journal_files", 4}, {"production_files_tested", false}};
}

// 仅验证探针标记内四条固定记录；未知与 Active 均必须保留文件和日志。
nlohmann::json verify(const fs::path &root, bool recover)
{
    rejectLinks(root);
    require(readSmall(root / ".probe-owner") == marker, "probe_owner_required");
    const auto entries = nlohmann::json::parse(readSmall(root / ".probe-entries.json"));
    require(entries.is_array() && entries.size() == 4, "fixture_entries_invalid");
    for (std::size_t i = 0; i < owners.size(); ++i) {
        const auto &entry = entries.at(i);
        const auto local = entry.at("path").get<std::string>();
        require(entry.at("owner").get<std::string>() == owners[i] && entry.at("id").get<std::uint64_t>() == i + 1, "fixture_identity_invalid");
        const auto relative = fs::path(local);
        require(relative.is_relative() && relative.begin()->string() == owners[i] && std::distance(relative.begin(), relative.end()) == 2, "fixture_path_invalid");
        require(entry.at("previous").get<std::string>() == std::string(owners[i]) + "-previous.bin", "fixture_previous_invalid");
        require(readSmall(root / relative) == "journal-fixture-" + std::string(owners[i]), "fixture_content_mismatch");
        require(readSmall(root / entry.at("previous").get<std::string>()) == "old-" + std::string(owners[i]), "previous_content_mismatch");
    }
    // 递归确认探针根内没有链接，恢复器只允许处理自己的四条日志。
    std::size_t journals = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        rejectLinks(entry.path());
        if (entry.path().extension() == ".pending") ++journals;
    }
    require(journals == 4, "fixture_journal_count_mismatch");
    files::FileStorage storage(root, false);
    std::size_t inspected = 0;
    const auto recovered = storage.recoverOwnedResourceWrites([&](const std::string &owner, std::uint64_t id, std::uint64_t generation, const std::string &path) -> std::optional<model::ResourceWriteDisposition> {
        require(id >= 1 && id <= owners.size() && owner == owners[id - 1] && generation == 7 && path == entries.at(id - 1).at("path").get<std::string>(), "journal_identity_mismatch");
        ++inspected;
        if (!recover) return std::nullopt;
        if (owner == "song") return model::ResourceWriteDisposition::Committed;
        if (owner == "singer") return model::ResourceWriteDisposition::Active;
        if (owner == "album") return model::ResourceWriteDisposition::Discard;
        return std::nullopt;
    });
    require(inspected == 4 && recovered == (recover ? 2 : 0), "recovery_count_mismatch");
    for (std::size_t i = 0; i < owners.size(); ++i) {
        const auto &entry = entries.at(i);
        require(fs::exists(root / entry.at("path").get<std::string>()) == (!recover || i != 2), "recovery_current_mismatch");
        require(fs::exists(root / entry.at("previous").get<std::string>()) == (!recover || i != 0), "recovery_previous_mismatch");
    }
    journals = 0;
    for (const auto &entry : fs::directory_iterator(root / ".resource-writes")) {
        if (entry.path().extension() == ".pending") ++journals;
    }
    require(journals == (recover ? 2 : 4), "remaining_journal_count_mismatch");
    return {{"ok", true}, {"execution", "actual_filestorage_controlled_fixture"}, {"owners_inspected", inspected}, {"recovered", recovered}, {"remaining_journals", journals}, {"production_files_tested", false}};
}
} // namespace

// 父脚本负责独占目录和备份还原；本探针不连接数据库、不启动 worker。
int main(int argc, char **argv)
{
    try {
        require(argc == 3, "usage_seed_verify_recover_root");
        const std::string_view mode = argv[1];
        require(mode == "--seed" || mode == "--verify" || mode == "--recover", "unknown_mode");
        const auto root = fs::absolute(fs::u8path(argv[2])).lexically_normal();
        std::cout << (mode == "--seed" ? seed(root) : verify(root, mode == "--recover")).dump() << '\n';
        return 0;
    } catch (const std::exception &) {
        std::cerr << "resource_file_journal_probe_failed\n";
        return 1;
    }
}
