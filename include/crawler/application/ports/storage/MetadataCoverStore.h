#pragma once

#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/application/model/MetadataCover.h"

#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 封面内容保存 port；实现应限制响应大小，完整保存后才返回成功。
class MetadataCoverStore {
public:
    // 释放保存器。
    virtual ~MetadataCoverStore() = default;

    // 下载已采集的图片地址；兼容入口 owner_kind 仅允许 singers、albums。
    virtual model::MetadataResourceFile save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url) = 0;

    // 独立下载携带认领身份；旧 adapter 仍可通过兼容入口工作。
    virtual model::MetadataResourceFile save(model::MetadataCoverSaveRequest request)
    {
        const auto &target = request.target;
        return save(target.source, target.owner == model::CoverOwner::Singer ? "singers" : "albums", target.source_id, target.remote_url);
    }

    // 数据库确认后解除本次封面文件补偿。
    virtual void accept(std::string_view)
    {}

    // 提交结果不确定时保留文件与日志，等待启动核对。
    virtual void defer(std::string_view)
    {}

    // 数据库事务失败时清理本次新建文件，不触碰此前已提交的文件。
    virtual bool discard(std::string_view local_path) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
