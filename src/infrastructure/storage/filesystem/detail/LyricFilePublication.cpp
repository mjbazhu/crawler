#include "crawler/infrastructure/storage/filesystem/detail/LyricFilePublication.h"

#include <openssl/rand.h>

#include <array>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::detail {
namespace {

namespace fs = std::filesystem;
using Result = application::result::ServiceResult<bool>;
using Error = application::result::ServiceErrorType;

// 发布过程的文件状态，供失败后反向恢复，不操作本次文件集合以外的路径。
struct Replacement {
    fs::path target;        // 固定目标文件
    fs::path staged;        // 已写完的新文件
    fs::path backup;        // 本次保留的旧文件
    bool backed_up = false; // 旧文件已移入备份
    bool installed = false; // 新文件已移入目标
};

// 仅清理本次独占创建的临时目录；恢复失败时保留备份以便诊断或重试。
class StagingDirectory {
public:
    // root 和 path 必须均为绝对路径，且 path 已独占创建。
    StagingDirectory(fs::path root, fs::path path) : root_(std::move(root)), path_(std::move(path))
    {}

    // 只删除位于根目录内的本次 staging，不清理其他历史目录。
    ~StagingDirectory()
    {
        if (preserve_) return;
        std::error_code error;
        const auto resolved = fs::weakly_canonical(path_, error);
        if (!error && isWithinRoot(root_, resolved)) fs::remove_all(path_, error);
    }

    // 恢复未完成时不能删除仍持有旧文件的目录。
    void preserve()
    {
        preserve_ = true;
    }

private:
    fs::path root_;         // 清理边界
    fs::path path_;         // 本次独占的临时目录
    bool preserve_ = false; // 是否保留恢复所需文件
};

// 不只检查文件存在；相同内容重试不改写文件和修改时间。
bool sameFile(const fs::path &path, std::string_view expected)
{
    if (!fs::is_regular_file(path) || fs::file_size(path) != expected.size()) return false;
    std::ifstream stream(path, std::ios::binary);
    std::string actual(expected.size(), '\0');
    stream.read(actual.data(), static_cast<std::streamsize>(actual.size()));
    return stream && actual == expected;
}

// 写完后关闭并检查磁盘错误，不对外暴露部分文件。
bool writeFile(const fs::path &path, std::string_view content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close();
    return !stream.fail();
}

// 只回滚已替换的目标；任一恢复失败时保留剩余备份，禁止继续清理。
bool rollback(std::vector<Replacement> &replacements)
{
    bool restored = true;
    for (auto iter = replacements.rbegin(); iter != replacements.rend(); ++iter) {
        std::error_code error;
        if (iter->installed) {
            fs::remove(iter->target, error);
            if (error) {
                restored = false;
                continue;
            }
        }
        if (iter->backed_up) {
            fs::rename(iter->backup, iter->target, error);
            if (error) restored = false;
        }
    }
    return restored;
}

// 提交前的异常也必须恢复旧文件，不能让 staging 清理误删唯一备份。
class ReplacementGuard {
public:
    // 状态和 staging 均由调用方持有，且生命周期长于当前守卫。
    ReplacementGuard(std::vector<Replacement> &replacements, StagingDirectory &staging) : replacements_(replacements), staging_(staging)
    {}

    // 未提交时自动恢复，恢复不完整则保留备份目录。
    ~ReplacementGuard()
    {
        if (!finished_) restore();
    }

    // 显式恢复允许调用方保留准确的失败原因，且不会重复回滚。
    bool restore()
    {
        finished_ = true;
        const bool restored = rollback(replacements_);
        if (!restored) staging_.preserve();
        return restored;
    }

    // 所有产物均已替换后才允许清理备份。
    void commit()
    {
        finished_ = true;
    }

private:
    std::vector<Replacement> &replacements_; // 当前集合的替换状态
    StagingDirectory &staging_;              // 失败时保留备份的清理器
    bool finished_ = false;                  // 是否已提交或显式恢复
};

// 独占目录名不携带版本或来源 hash，临时文件不属于稳定对外路径。
std::string stagingName()
{
    std::array<unsigned char, 16> random {};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) return {};
    constexpr char digits[] = "0123456789abcdef";
    std::string name = ".lyrics-stage-";
    for (const auto value : random) {
        name += digits[value >> 4];
        name += digits[value & 15];
    }
    return name;
}

} // namespace

bool isWithinRoot(const fs::path &root, const fs::path &path)
{
    auto current = path.begin();
    for (const auto &part : root) {
        if (current == path.end() || *current++ != part) return false;
    }
    return current != path.end();
}

Result publishLyricFiles(const fs::path &root, const fs::path &directory, const std::vector<LyricFileContent> &files)
{
    try {
        const auto parent = fs::weakly_canonical(directory);
        if ((parent != root && !isWithinRoot(root, parent)) || files.empty()) return Result::failure(Error::InvalidParam, "lyric_unsafe_output_path");
        bool matches = true;
        for (const auto &file : files) {
            const fs::path name(file.filename);
            const auto target = parent / name;
            if (name.empty() || name != name.filename() || name == "." || name == ".." || fs::is_symlink(fs::symlink_status(target)) || !isWithinRoot(root, fs::weakly_canonical(target))) return Result::failure(Error::InvalidParam, "lyric_unsafe_output_path");
            if (fs::exists(target) && !fs::is_regular_file(target)) return Result::failure(Error::Conflict, "lyric_artifact_conflict");
            matches = sameFile(target, file.content) && matches;
        }
        if (matches) return Result::success(false);
        fs::create_directories(parent);
        const auto name = stagingName();
        if (name.empty()) return Result::failure(Error::InternalError, "lyric_staging_failed");
        const auto staging = parent / name;
        if (!fs::create_directory(staging)) return Result::failure(Error::Conflict, "lyric_staging_failed");
        StagingDirectory cleanup(root, staging);
        fs::create_directory(staging / "backup");
        std::vector<Replacement> replacements;
        replacements.reserve(files.size());
        for (const auto &file : files) {
            replacements.push_back({parent / file.filename, staging / file.filename, staging / "backup" / file.filename});
            if (!writeFile(replacements.back().staged, file.content)) return Result::failure(Error::InternalError, "lyric_artifact_write_failed");
        }
        ReplacementGuard guard(replacements, cleanup);
        for (auto &replacement : replacements) {
            std::error_code error;
            const bool exists = fs::exists(replacement.target, error);
            if (!error && exists) {
                fs::rename(replacement.target, replacement.backup, error);
                replacement.backed_up = !error;
            }
            if (!error) {
                fs::rename(replacement.staged, replacement.target, error);
                replacement.installed = !error;
            }
            if (error) {
                if (!guard.restore()) {
                    return Result::failure(Error::InternalError, "lyric_artifact_rollback_failed");
                }
                return Result::failure(Error::InternalError, "lyric_artifact_publish_failed");
            }
        }
        guard.commit();
        return Result::success(true);
    } catch (const fs::filesystem_error &) {
        return Result::failure(Error::InternalError, "lyric_artifact_io_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::detail
