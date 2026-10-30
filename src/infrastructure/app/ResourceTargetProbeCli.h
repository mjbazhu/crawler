#pragma once
#include "crawler/infrastructure/app/ServerApplication.h"

#include <map>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app {
// 与服务共用既有RC/百度和parallel配置，不改写私有环境文件。
ServerApplication::Config resourceArchiveConfiguration();
// 独立只读探测，不连接数据库、创建根目录或启动后台任务。
int resourceTargetProbeCommand(const std::map<std::string, std::string> &options);
} // namespace orchestrator::modules::crawler::infrastructure::app
