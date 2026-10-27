#pragma once

#include "crawler/application/ports/execution/ResourcePostProcessor.h"
#include "crawler/application/ports/parsing/TimedLyricsDecoder.h"
#include "crawler/application/ports/persistence/LyricArtifactRepository.h"
#include "crawler/application/ports/storage/LyricArtifactEncoder.h"
#include "crawler/application/ports/storage/LyricArtifactStore.h"

#include <memory>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 歌词用例依赖；具体 decoder、文件系统和数据库仅在组合根装配。
struct LyricProcessingDependencies {
    std::shared_ptr<ports::TimedLyricsDecoder> decoder;         // 平台内容解码
    std::shared_ptr<ports::LyricArtifactEncoder> encoder;       // 标准文件导出
    std::shared_ptr<ports::LyricArtifactStore> store;           // 文件提交与来源读取
    std::shared_ptr<ports::LyricArtifactRepository> repository; // 可选数据库索引，离线 CLI 可省略
};

// 独立歌词处理用例；只共享不可变依赖，可并发执行不同来源文件。
class LyricProcessingWorkflow final : public ports::ResourcePostProcessor {
public:
    // source 标识只用于路由已下载资源，不承担平台请求逻辑。
    LyricProcessingWorkflow(LyricProcessingDependencies dependencies, std::string source);
    // 供在线资源完成回调复用；不支持的来源或资源类型直接跳过。
    result::ServiceResult<bool> process(const model::SongResourceProcessItem &item) const override;
    // 供离线历史文件处理复用；任何错误均不修改原始文件。
    result::ServiceResult<model::LyricArtifactBundle> processFile(const model::LyricProcessingRequest &request) const;

private:
    LyricProcessingDependencies dependencies_; // 创建后不变的窄 port
    std::string source_;                       // 本实例支持的平台
};

} // namespace orchestrator::modules::crawler::application::use_cases
