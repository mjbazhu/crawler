#pragma once

#include "crawler/application/model/MetadataWriteBatch.h"

namespace orchestrator::modules::crawler::application::use_cases {

// 从一次响应中分离实体输入；歌曲内的专辑和歌手属性仅在此转换，不写回歌曲表。
application::model::MetadataWriteBatch buildMetadataWriteBatch(application::model::MetadataWriteBatch input);

} // namespace orchestrator::modules::crawler::application::use_cases
