#pragma once

#include "crawler/application/use_cases/LyricProcessingWorkflow.h"

namespace orchestrator::modules::crawler::api::cli {

// 歌词 CLI 协议入口，只负责参数和返回码，业务通过 application 用例执行。
int runLyricsCommand(int count, char **relative_paths, application::use_cases::LyricProcessingWorkflow &workflow);

} // namespace orchestrator::modules::crawler::api::cli
