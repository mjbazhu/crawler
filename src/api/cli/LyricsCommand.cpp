#include "crawler/api/cli/LyricsCommand.h"

#include "crawler/application/use_cases/LyricProcessingWorkflow.h"

#include <iostream>

namespace orchestrator::modules::crawler::api::cli {

int runLyricsCommand(int count, char **relative_paths, application::use_cases::LyricProcessingWorkflow &workflow)
{
    bool failed = false;
    for (int index = 0; index < count; ++index) {
        application::model::LyricProcessingRequest request;
        request.local_path = relative_paths[index];
        const auto result = workflow.processFile(request);
        if (!result.isSuccess()) {
            std::cerr << "lyrics: " << result.message() << '\n';
            failed = true;
            continue;
        }
        for (const auto &file : result.data()->files) std::cout << file.kind << '\t' << file.local_path << '\n';
    }
    return failed ? 1 : 0;
}

} // namespace orchestrator::modules::crawler::api::cli
