#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "FileWriteSupport.h"
#include "SongResourceFileSession.h"

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

using namespace file_detail; // 本 adapter 的路径和原子写入细节

FileStorage::FileStorage(std::filesystem::path root_dir, bool create_root) : root_dir_(std::move(root_dir))
{
    if (!create_root) return;
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
        if (auto rename_error = renameReplacingWithRetry(temp_path, full_path); !rename_error.empty()) {
            std::cerr << "[FileStorage] Rename failed for: " << temp_path << " -> " << full_path << " Error: " << rename_error << std::endl;
            removeQuietly(temp_path);
            return false;
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
    return file_detail::openSongResourceWrite(root_dir_, std::move(request));
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
