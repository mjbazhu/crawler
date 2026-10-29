#pragma once

#include "crawler/application/model/CloudFile.h"

#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全的已提交文件云端存储 port；不接受任意远端下载 URL。
class CloudFileStore {
public:
    // 释放存储后端。
    virtual ~CloudFileStore() = default;
    // 返回稳定后端名称。
    virtual std::string backend() const = 0;
    // 返回带尾部分隔符的目标根，用于识别配置变化后的补传。
    virtual std::string prefix() const = 0;
    // 已有对象先核验，上传后再次核验；只有内容一致才返回成功。
    virtual model::CloudFileResult upload(model::CloudFileUploadRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
