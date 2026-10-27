#include "crawler/api/cli/LyricsCommand.h"

#include "crawler/infrastructure/app/LyricProcessingComposition.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

// 离线歌词程序的组合根，不创建网络客户端或数据库连接。
int main(int argc, char **argv)
{
    if (argc < 3 || std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: crawler_lyrics <storage-root> <relative.mrc> [relative.mrc ...]\n"
                     "Set CRAWLER_MIGU_MRC_KEY in the environment. Offline files and manifests only; no database write.\n";
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 2;
    }
    const char *key = std::getenv("CRAWLER_MIGU_MRC_KEY");
    if (!key || !*key) {
        std::cerr << "lyrics: mrc_key_not_configured\n";
        return 2;
    }
    try {
        auto workflow = orchestrator::modules::crawler::infrastructure::app::makeMiguLyricProcessingWorkflow(argv[1], key);
        return orchestrator::modules::crawler::api::cli::runLyricsCommand(argc - 2, argv + 2, *workflow);
    } catch (const std::exception &) {
        std::cerr << "lyrics: initialization_failed\n";
        return 1;
    }
}
