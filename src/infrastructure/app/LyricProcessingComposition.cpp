#include "crawler/infrastructure/app/LyricProcessingComposition.h"

#include "crawler/infrastructure/music/lyrics/TimedLyricsEncoder.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcLyricsDecoder.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLyricArtifactRepository.h"
#include "crawler/infrastructure/storage/filesystem/LocalLyricArtifactStore.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {

std::shared_ptr<application::use_cases::LyricProcessingWorkflow> makeMiguLyricProcessingWorkflow(const std::filesystem::path &root, std::string_view key, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool)
{
    application::use_cases::LyricProcessingDependencies dependencies;
    dependencies.decoder = std::make_shared<music::migu::lyrics::MiguMrcLyricsDecoder>(key);
    dependencies.encoder = std::make_shared<music::lyrics::TimedLyricsEncoder>();
    dependencies.store = std::make_shared<storage::filesystem::LocalLyricArtifactStore>(root);
    if (pool) dependencies.repository = std::make_shared<music::persistence::mysql::MySqlLyricArtifactRepository>(std::move(pool));
    return std::make_shared<application::use_cases::LyricProcessingWorkflow>(std::move(dependencies), "migu");
}

} // namespace orchestrator::modules::crawler::infrastructure::app
