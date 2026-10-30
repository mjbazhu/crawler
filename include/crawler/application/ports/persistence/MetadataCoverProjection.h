#pragma once
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/result/ServiceResult.h"
namespace orchestrator::modules::crawler::application::ports {
// 并行模式固定旧平台的只读视图，不能进入旧claim/upload路径。
class MetadataCoverProjection {
public:
    virtual ~MetadataCoverProjection() = default;
    virtual result::ServiceResult<model::MetadataCoverPage> list(const model::MetadataCoverQuery &query) = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
