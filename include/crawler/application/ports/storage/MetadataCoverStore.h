#pragma once

#include "crawler/application/model/MetadataResourceFile.h"

#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 封面内容保存 port；实现应限制响应大小，完整保存后才返回成功。
class MetadataCoverStore {
public:
    // 释放保存器。
    virtual ~MetadataCoverStore() = default;

    // 下载本次响应给出的地址；owner_kind 仅允许 singers、albums。
    virtual model::MetadataResourceFile save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url) = 0;

    // 数据库事务失败时清理本次新建文件，不触碰此前已提交的文件。
    virtual bool discard(std::string_view local_path) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
