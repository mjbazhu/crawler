#pragma once
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/model/SongResourceQueue.h"
#include <string>
#include <cstdint>
#include <variant>
#include <optional>
namespace orchestrator::modules::crawler::application::model {
// 派生身份允许从原 MRC 再生，正式 local 文件不是必要前置。
struct ResourceArtifactInput {
    std::string local_path;                        // 正式根内路径
    std::string checksum;                          // 原始算法，按兼容规则核对
    std::uint64_t size_bytes = 0;                  // 完整大小
    std::string artifact_type;                     // word_json/enhanced_lrc
    std::optional<SongResourceQueueTarget> source; // 已登记父 MRC 的受控来源
    std::string source_checksum;                   // 生成该产物所用 MRC 的实际 SHA-256
    std::string processor_version;                 // 必须匹配当前解码/导出版本
    int schema_version = 1;                        // 产物协议版本
};
// 描述只来自资源库，不接收 API 提供的 URL 或文件。
using ResourceTransferSourceInput = std::variant<SongResourceQueueTarget, MetadataCoverRecord, ResourceArtifactInput>;
} // namespace orchestrator::modules::crawler::application::model
