#pragma once
#include <string>
#include <string_view>
#include <system_error>

#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace file_detail {
// 检查相对路径是否不会逃逸出存储根目录。
inline bool isSafeRelativePath(std::string_view relative_path)
{
    if (relative_path.empty()) {
        return false;
    }
    std::filesystem::path path {std::string(relative_path)};
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto &part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

// 将 SHA-256 摘要转换为十六进制字符串。
inline std::string sha256DigestHex(const std::array<unsigned char, SHA256_DIGEST_LENGTH> &digest)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        output << std::setw(2) << static_cast<int>(byte);
    }
    return output.str();
}

// 为同一路径的并发保存生成独立临时文件，避免固定 .tmp 被互相覆盖。
inline std::filesystem::path makeUniqueTempPath(const std::filesystem::path &full_path)
{
    std::ostringstream suffix;
    suffix << ".tmp." << std::chrono::steady_clock::now().time_since_epoch().count() << '.' << std::this_thread::get_id();
    auto temp_path = full_path;
    temp_path += suffix.str();
    return temp_path;
}

// 尽力清理临时文件，失败不覆盖原始保存错误。
inline void removeQuietly(const std::filesystem::path &path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// Windows 下刚关闭文件后 rename 偶尔会被短暂占用，提交时做有限重试。
inline std::string renameReplacingWithRetry(const std::filesystem::path &temp_path, const std::filesystem::path &full_path)
{
    constexpr int kMaxAttempts = 240;
    std::string last_error;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        std::error_code rename_ec;
        std::filesystem::rename(temp_path, full_path, rename_ec);
        if (!rename_ec) {
            return {};
        }
        last_error = rename_ec.message();

        std::error_code exists_ec;
        if (std::filesystem::exists(full_path, exists_ec)) {
            std::error_code remove_ec;
            std::filesystem::remove(full_path, remove_ec);
            if (remove_ec) {
                last_error = remove_ec.message();
                if (attempt + 1 == kMaxAttempts) {
                    std::cerr << "[FileStorage] Replace failed after retries: " << temp_path << " -> " << full_path << " Error: " << last_error << std::endl;
                    return "file_replace_failed";
                }
            } else {
                rename_ec.clear();
                std::filesystem::rename(temp_path, full_path, rename_ec);
                if (!rename_ec) {
                    return {};
                }
                last_error = rename_ec.message();
            }
        } else if (!exists_ec) {
            std::error_code copy_ec;
            std::filesystem::copy_file(temp_path, full_path, std::filesystem::copy_options::overwrite_existing, copy_ec);
            if (!copy_ec) {
                removeQuietly(temp_path);
                return {};
            }
            last_error = copy_ec.message();
        } else {
            last_error = exists_ec.message();
        }

        if (attempt + 1 < kMaxAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    std::cerr << "[FileStorage] Rename failed after retries: " << temp_path << " -> " << full_path << " Error: " << last_error << std::endl;
    return "file_rename_failed";
}

}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::file_detail
