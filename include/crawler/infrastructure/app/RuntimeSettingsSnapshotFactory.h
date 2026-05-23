#pragma once

#include "crawler/api/dto/CrawlerSettingsDto.h"
#include "crawler/application/model/CrawlerRuntimeSettings.h"
#include "crawler/infrastructure/app/ServerApplication.h"

namespace orchestrator::modules::crawler::infrastructure::app {

// 生成 application 可热更新的运行设置。
application::model::CrawlerRuntimeSettings makeRuntimeSettings(const ServerApplication::Config &config);

// 生成 settings API 兼容只读快照。
api::dto::CrawlerSettingsReadonlySnapshot makeReadonlySettingsSnapshot(const ServerApplication::Config &config);

} // namespace orchestrator::modules::crawler::infrastructure::app
