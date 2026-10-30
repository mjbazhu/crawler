#pragma once

#include "crawler/application/model/ResourceTransferSource.h"
#include "crawler/application/ports/parsing/TimedLyricsDecoder.h"
#include "crawler/application/ports/storage/LyricArtifactEncoder.h"
#include "crawler/application/ports/parsing/ResourceArtifactGenerator.h"

#include <memory>
#include <optional>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 三端共享同内容/版本的有界生成缓存，不创建正式本地目录。
class ResourceArtifactProducer final : public application::ports::ResourceArtifactGenerator {
public:
    // 只注入纯解码与导出，持久任务完全由独立 worker 调度。
    ResourceArtifactProducer(std::shared_ptr<application::ports::TimedLyricsDecoder> decoder, std::shared_ptr<application::ports::LyricArtifactEncoder> encoder, bool available = true);
    // 只描述启动能力；缺密钥时再生来源不能先访问网络。
    bool available() const;
    // 返回完整产物参考和空正式路径，不登记或排队子操作。
    application::result::ServiceResult<application::model::LyricArtifactBundle> generate(const application::model::ResourceTransferJob &parent, const application::model::ResourceContentSnapshot &source, const application::model::ResourceTransferContext &context) override;
    // 已生成内容供两种产物/各端复用，返回副本不共享可变正文。
    std::optional<application::model::EncodedLyricArtifact> cached(const application::model::ResourceArtifactInput &input) const;
    // 缓存未命中时从受控 MRC 私有快照重建，严格核对旧来源/处理版本。
    application::result::ServiceResult<application::model::EncodedLyricArtifact> regenerate(const application::model::ResourceArtifactInput &input, const application::model::ResourceContentSnapshot &source, const application::model::ResourceTransferContext &context);

private:
    class State;
    std::shared_ptr<State> state_; // 缓存和纯解码依赖共同生存，目标停止不会删除兄弟内容
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
