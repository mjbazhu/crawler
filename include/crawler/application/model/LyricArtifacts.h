#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 歌词后处理输入；路径只能指向已下载并提交的来源文件。
struct LyricProcessingRequest {
    std::uint64_t source_resource_id = 0; // 原始 MRC 的 song_resources.id；离线处理可为 0
    std::string local_path;               // 存储根目录内相对路径
    std::string expected_checksum;        // 已入库来源的 SHA-256；离线处理可为空
};

// 受限读取的歌词来源；checksum 由存储 adapter 计算。
struct LyricSourceContent {
    std::string content;  // 原始来源字节
    std::string checksum; // 完整来源 SHA-256
};

// 一个序列化后的歌词产物，正文只交付存储，不进入资源索引表。
struct EncodedLyricArtifact {
    std::string kind;         // word_json 或 enhanced_lrc
    std::string filename;     // 固定的安全文件名
    std::string content_type; // 文件媒体类型
    std::string content;      // UTF-8 无 BOM 正文
};

// 单个已完整提交的产物描述。
struct LyricArtifactFile {
    std::string kind;             // 产物类型
    std::string local_path;       // 存储根目录内相对路径
    std::uint64_t size_bytes = 0; // 完整文件大小
    std::string checksum;         // 文件 SHA-256
};

// 一次来源处理的完整产物集合，供文件 manifest 和数据库索引共同使用。
struct LyricArtifactBundle {
    std::uint64_t source_resource_id = 0; // 原始资源 ID
    std::string source_checksum;          // 本次输入 SHA-256
    std::string processor_version;        // decoder 及导出协议版本
    int schema_version = 1;               // JSON 文档结构版本
    std::vector<LyricArtifactFile> files; // 每个产物独立的路径、大小和校验值
};

} // namespace orchestrator::modules::crawler::application::model
