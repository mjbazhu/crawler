#pragma once

#include "crawler/application/model/MetadataWriteBatch.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/use_cases/ParserDispatch.h"

#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 当前详情包含完整首段时补齐后续曲目页；任务内状态独占，失败保留已获取数据并返回原因。
std::string collectRemainingAlbumTracks(const application::model::AlbumSourceMetadata &album, application::model::MetadataWriteBatch &batch, application::ports::AlbumDetailProvider &provider, ParserDispatch &parser);

} // namespace orchestrator::modules::crawler::application::use_cases
