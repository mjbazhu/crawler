#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

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

namespace {

// 检查相对路径是否不会逃逸出存储根目录。
bool isSafeRelativePath(std::string_view relative_path)
{
    if (relative_path.empty()) {
        return false;
    }
    std::filesystem::path path {std::string(relative_path)};
    if (path.is_absolute()) {
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
std::string sha256DigestHex(const std::array<unsigned char, SHA256_DIGEST_LENGTH> &digest)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        output << std::setw(2) << static_cast<int>(byte);
    }
    return output.str();
}

// 为同一路径的并发保存生成独立临时文件，避免固定 .tmp 被互相覆盖。
std::filesystem::path makeUniqueTempPath(const std::filesystem::path &full_path)
{
    std::ostringstream suffix;
    suffix << ".tmp." << std::chrono::steady_clock::now().time_since_epoch().count() << '.' << std::this_thread::get_id();
    auto temp_path = full_path;
    temp_path += suffix.str();
    return temp_path;
}

// 尽力清理临时文件，失败不覆盖原始保存错误。
void removeQuietly(const std::filesystem::path &path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// 本地文件流式写入会话，先写临时文件，commit 成功后才替换最终路径。
class FileSongResourceBlobWriteSession : public application::ports::SongResourceBlobWriteSession {
public:
    FileSongResourceBlobWriteSession(std::string relative_path, std::filesystem::path full_path, std::filesystem::path temp_path) : relative_path_(std::move(relative_path)),
                                                                                                                                    full_path_(std::move(full_path)),
                                                                                                                                    temp_path_(std::move(temp_path)),
                                                                                                                                    output_(temp_path_, std::ios::binary | std::ios::out | std::ios::trunc)
    {
        SHA256_Init(&sha_context_);
    }

    ~FileSongResourceBlobWriteSession() override
    {
        if (!committed_) {
            abort();
        }
    }

    bool opened() const
    {
        return output_.is_open();
    }

    bool write(std::string_view chunk, std::string &error) override
    {
        if (committed_ || aborted_) {
            error = "blob_write_session_closed";
            return false;
        }
        if (!output_.is_open()) {
            error = "temp_file_open_failed";
            return false;
        }
        if (chunk.empty()) {
            return true;
        }
        if (chunk.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
            error = "chunk_too_large";
            return false;
        }

        output_.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (!output_) {
            error = "file_write_failed";
            return false;
        }
        SHA256_Update(&sha_context_, reinterpret_cast<const unsigned char *>(chunk.data()), chunk.size());
        size_bytes_ += static_cast<std::uint64_t>(chunk.size());
        return true;
    }

    application::model::SongResourceBlobSaveResult commit() override
    {
        application::model::SongResourceBlobSaveResult result;
        if (committed_ || aborted_) {
            result.error = "blob_write_session_closed";
            return result;
        }
        if (size_bytes_ == 0) {
            result.error = "empty_resource_body";
            abort();
            return result;
        }

        output_.flush();
        if (!output_) {
            result.error = "file_flush_failed";
            abort();
            return result;
        }
        output_.close();
        if (!output_) {
            result.error = "file_close_failed";
            abort();
            return result;
        }

        std::error_code rename_ec;
        std::filesystem::rename(temp_path_, full_path_, rename_ec);
        if (rename_ec) {
            std::error_code exists_ec;
            if (std::filesystem::exists(full_path_, exists_ec)) {
                std::error_code remove_ec;
                std::filesystem::remove(full_path_, remove_ec);
                if (remove_ec) {
                    result.error = "file_replace_failed";
                    abort();
                    return result;
                }
                rename_ec.clear();
                std::filesystem::rename(temp_path_, full_path_, rename_ec);
            }
            if (rename_ec) {
                result.error = "file_rename_failed";
                abort();
                return result;
            }
        }

        std::error_code size_ec;
        const auto saved_size = std::filesystem::file_size(full_path_, size_ec);
        if (size_ec || saved_size != size_bytes_) {
            result.error = "file_size_verify_failed";
            removeQuietly(full_path_);
            committed_ = true;
            return result;
        }

        std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
        SHA256_Final(digest.data(), &sha_context_);
        committed_ = true;

        result.ok = true;
        result.local_path = relative_path_;
        result.size_bytes = size_bytes_;
        result.checksum = sha256DigestHex(digest);
        return result;
    }

    void abort() override
    {
        if (aborted_ || committed_) {
            return;
        }
        aborted_ = true;
        if (output_.is_open()) {
            output_.close();
        }
        removeQuietly(temp_path_);
    }

private:
    std::string relative_path_;       // 存储相对路径
    std::filesystem::path full_path_; // 最终文件路径
    std::filesystem::path temp_path_; // 临时文件路径
    std::ofstream output_;            // 临时文件输出流
    SHA256_CTX sha_context_ {};       // 流式 SHA-256 上下文
    std::uint64_t size_bytes_ = 0;    // 已写入字节数
    bool committed_ = false;          // 是否已提交到最终路径
    bool aborted_ = false;            // 是否已放弃写入
};

} // namespace

FileStorage::FileStorage(std::filesystem::path root_dir) : root_dir_(std::move(root_dir))
{
    // 构造时确保根目录存在
    std::error_code ec;
    if (!std::filesystem::exists(root_dir_, ec)) {
        std::filesystem::create_directories(root_dir_, ec);
        if (ec) {
            std::cerr << "[FileStorage] Failed to create root dir: " << root_dir_ << " Error: " << ec.message() << std::endl;
        }
    }
}

std::filesystem::path FileStorage::getFullPath(std::string_view relative_path) const
{
    // 使用 / 运算符安全拼接路径
    return root_dir_ / relative_path;
}

bool FileStorage::save(std::string_view relative_path, const std::vector<char> &data)
{
    try {
        if (!isSafeRelativePath(relative_path)) {
            std::cerr << "[FileStorage] Unsafe relative path: " << relative_path << std::endl;
            return false;
        }
        auto full_path = getFullPath(relative_path);
        auto temp_path = makeUniqueTempPath(full_path);

        // 2. 确保父目录存在
        if (auto parent = full_path.parent_path(); !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[FileStorage] Failed to create parent dir: " << parent << " Error: " << ec.message() << std::endl;
                return false;
            }
        }

        // 3. 写入临时文件
        {
            // 使用二进制模式打开，截断旧内容
            std::ofstream ofs(temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!ofs.is_open()) {
                std::cerr << "[FileStorage] Failed to open temp file: " << temp_path << std::endl;
                return false;
            }

            ofs.write(data.data(), data.size());
            if (!ofs) {
                std::cerr << "[FileStorage] Write failed for: " << temp_path << std::endl;
                ofs.close();
                removeQuietly(temp_path);
                return false;
            }

            ofs.flush();
            if (!ofs) {
                std::cerr << "[FileStorage] Flush failed for: " << temp_path << std::endl;
                ofs.close();
                removeQuietly(temp_path);
                return false;
            }
            ofs.close();
            if (!ofs) {
                std::cerr << "[FileStorage] Close failed for: " << temp_path << std::endl;
                removeQuietly(temp_path);
                return false;
            }
        } // ofs 在此处析构，文件关闭

        // 4. 将完整临时文件提交到最终路径；Windows 下目标存在时需要先移除。
        std::error_code rename_ec;
        std::filesystem::rename(temp_path, full_path, rename_ec);
        if (rename_ec) {
            std::error_code exists_ec;
            if (std::filesystem::exists(full_path, exists_ec)) {
                std::error_code remove_ec;
                std::filesystem::remove(full_path, remove_ec);
                if (remove_ec) {
                    std::cerr << "[FileStorage] Failed to replace target file: " << full_path << " Error: " << remove_ec.message() << std::endl;
                    removeQuietly(temp_path);
                    return false;
                }
                rename_ec.clear();
                std::filesystem::rename(temp_path, full_path, rename_ec);
            }
            if (rename_ec) {
                std::cerr << "[FileStorage] Rename failed for: " << temp_path << " -> " << full_path << " Error: " << rename_ec.message() << std::endl;
                removeQuietly(temp_path);
                return false;
            }
        }

        // 5. 提交后校验最终文件，避免数据库回写一个不可见或不完整的文件。
        std::error_code size_ec;
        const auto saved_size = std::filesystem::file_size(full_path, size_ec);
        if (size_ec || saved_size != data.size()) {
            std::cerr << "[FileStorage] Saved file verification failed: " << full_path << std::endl;
            removeQuietly(full_path);
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        std::cerr << "[FileStorage] Save exception: " << e.what() << std::endl;
        return false;
    }
}

bool FileStorage::remove(std::string_view relative_path)
{
    try {
        if (!isSafeRelativePath(relative_path)) {
            return false;
        }
        auto full_path = getFullPath(relative_path);
        // remove 返回是否删除了文件 (若文件不存在返回 false)
        return std::filesystem::remove(full_path);
    } catch (const std::exception &e) {
        std::cerr << "[FileStorage] Remove exception: " << e.what() << std::endl;
        return false;
    }
}

bool FileStorage::exists(std::string_view relative_path) const
{
    try {
        if (!isSafeRelativePath(relative_path)) {
            return false;
        }
        return std::filesystem::exists(getFullPath(relative_path));
    } catch (...) {
        return false;
    }
}

application::model::SongResourceBlobSaveResult FileStorage::saveSongResourceBlob(application::model::SongResourceBlobSaveRequest request)
{
    application::model::SongResourceBlobSaveResult result;
    if (request.data.empty()) {
        result.error = "empty_resource_body";
        return result;
    }

    application::model::SongResourceBlobStreamSaveRequest stream_request;
    stream_request.relative_path_hint = request.relative_path_hint;
    stream_request.content_type = request.content_type;
    auto session = beginSongResourceBlobWrite(std::move(stream_request));
    if (!session) {
        result.error = "file_stream_open_failed";
        return result;
    }

    std::string write_error;
    if (!session->write(std::string_view(request.data.data(), request.data.size()), write_error)) {
        result.error = write_error.empty() ? "file_write_failed" : write_error;
        session->abort();
        return result;
    }

    return session->commit();
}

std::unique_ptr<application::ports::SongResourceBlobWriteSession> FileStorage::beginSongResourceBlobWrite(application::model::SongResourceBlobStreamSaveRequest request)
{
    try {
        if (request.relative_path_hint.empty() || !isSafeRelativePath(request.relative_path_hint)) {
            return nullptr;
        }

        auto full_path = getFullPath(request.relative_path_hint);
        if (auto parent = full_path.parent_path(); !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[FileStorage] Failed to create parent dir: " << parent << " Error: " << ec.message() << std::endl;
                return nullptr;
            }
        }

        auto session = std::make_unique<FileSongResourceBlobWriteSession>(request.relative_path_hint, full_path, makeUniqueTempPath(full_path));
        if (!session->opened()) {
            return nullptr;
        }
        return session;
    } catch (const std::exception &e) {
        std::cerr << "[FileStorage] Begin stream save exception: " << e.what() << std::endl;
        return nullptr;
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
