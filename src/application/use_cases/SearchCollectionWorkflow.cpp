#include "crawler/application/use_cases/SearchCollectionWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"

#include <future>
#include <optional>
#include <utility>
#include <variant>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 歌曲搜索分页采集结果。
struct SongPageResult {
    int page = 0;                   // 页码
    std::vector<domain::Song> data; // 本页歌曲
};

// 专辑搜索分页采集结果。
struct AlbumPageResult {
    int page = 0;                    // 页码
    std::vector<domain::Album> data; // 本页专辑
};

// 专辑歌曲采集结果。
struct AlbumSongsResult {
    int page = 0;                   // 用于复用分页排序 helper 的稳定序号
    std::size_t index = 0;          // 专辑下标
    std::vector<domain::Song> data; // 专辑歌曲
};

} // namespace

SearchCollectionWorkflow::SearchCollectionWorkflow(
    std::shared_ptr<application::ports::SongSearchProvider> song_search_provider,
    std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider,
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
    std::shared_ptr<ParserDispatch> parser,
    std::shared_ptr<MetadataPersistenceService> persistence,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
) : song_search_provider_(std::move(song_search_provider)),
    album_search_provider_(std::move(album_search_provider)),
    album_detail_provider_(std::move(album_detail_provider)),
    parser_(std::move(parser)),
    persistence_(std::move(persistence)),
    page_runner_(std::move(page_executor), std::move(diagnostics))
{}

bool SearchCollectionWorkflow::searchAndSave(std::string keyword, int limit, int max_page_concurrency)
{
    if (!song_search_provider_ || !album_search_provider_ || !parser_ || keyword.empty() || !persistence_) {
        return false;
    }

    const int normalized_limit = normalizeWorkflowLimit(limit);
    if (persistence_->hasAtomicWriter()) return searchSourcesAndSave(keyword, normalized_limit, max_page_concurrency);
    auto songs = collectSongs(keyword, normalized_limit, max_page_concurrency);
    auto albums = collectAlbums(std::move(keyword), normalized_limit, max_page_concurrency);

    if (persistence_->hasAtomicWriter()) return !songs.songs.empty() || !albums.albums.empty();
    bool saved = false;
    saved = persistence_->saveSongs(songs.songs, normalized_limit) || saved;
    saved = persistence_->saveAlbums(albums.albums, normalized_limit) || saved;
    return saved;
}

std::vector<domain::Song> SearchCollectionWorkflow::collectSongPage(std::string keyword, int page)
{
    auto response = song_search_provider_ ? song_search_provider_->fetchSongSearchPage(std::move(keyword), page, kWorkflowSearchRows) : std::nullopt;
    if (!response) {
        return {};
    }

    std::vector<char> body = std::move(response->body);
    auto song_sources = parser_->parseSongSources(body);
    if (!song_sources.empty() && !persistence_->saveSongSources(song_sources, kWorkflowSearchRows)) return {};

    auto results = parser_->parse(body);
    std::vector<domain::Song> songs;
    for (const auto &result : results) {
        if (std::holds_alternative<domain::Song>(result.meta_info)) {
            songs.push_back(std::get<domain::Song>(result.meta_info));
        }
    }
    return songs;
}

std::vector<domain::Album> SearchCollectionWorkflow::collectAlbumPage(std::string keyword, int page)
{
    auto response = album_search_provider_ ? album_search_provider_->fetchAlbumSearchPage(std::move(keyword), page, kWorkflowSearchRows) : std::nullopt;
    if (!response) {
        return {};
    }

    if (persistence_->hasAtomicWriter()) {
        auto sources = parser_->parseAlbumSources(response->body);
        if (sources.empty() || !persistence_->saveAlbumSources(sources, kWorkflowSearchRows)) return {};
    }
    auto results = parser_->parse(std::move(response->body));
    std::vector<domain::Album> albums;
    for (const auto &result : results) {
        if (std::holds_alternative<domain::Album>(result.meta_info)) {
            albums.push_back(std::get<domain::Album>(result.meta_info));
        }
    }
    return albums;
}

domain::SongList SearchCollectionWorkflow::collectSongs(std::string keyword, int limit, int max_page_concurrency)
{
    domain::SongList list;
    const int normalized_limit = normalizeWorkflowLimit(limit);
    const int last_page = (std::max)(1, (normalized_limit + kWorkflowSearchRows - 1) / kWorkflowSearchRows);

    for (int first_page = 1; first_page <= last_page && !reachedWorkflowLimit(list.songs.size(), normalized_limit); first_page += workflowPageWindow(max_page_concurrency)) {
        const int current_last_page = (std::min)(last_page, first_page + workflowPageWindow(max_page_concurrency) - 1);
        auto pages = collectOrderedPageWindow<SongPageResult>(
            page_runner_,
            first_page,
            current_last_page,
            [this, keyword](int page) {
                return SongPageResult {page, collectSongPage(keyword, page)};
            },
            "collect song page"
        );

        for (auto &page : pages) {
            for (auto &song : page.data) {
                list.songs.push_back(std::move(song));
                if (reachedWorkflowLimit(list.songs.size(), normalized_limit)) {
                    break;
                }
            }
        }
    }

    list.total = static_cast<int>(list.songs.size());
    return list;
}

std::vector<domain::Song> SearchCollectionWorkflow::collectAlbumSongs(const domain::Album &album)
{
    auto response = album_detail_provider_ ? album_detail_provider_->fetchAlbumSongs(album) : std::nullopt;
    if (!response) {
        return {};
    }

    std::vector<char> body = std::move(response->body);
    auto song_sources = parser_->parseSongSources(body);
    if (persistence_->hasAtomicWriter()) {
        auto sources = parser_->parseAlbumSources(body);
        if (!persistence_->saveAlbumDetails(sources, song_sources)) return {};
    } else if (!song_sources.empty() && !persistence_->saveSongSources(song_sources, kWorkflowSearchRows))
        return {};

    auto results = parser_->parse(body);
    std::vector<domain::Song> songs;
    for (const auto &result : results) {
        if (std::holds_alternative<domain::Song>(result.meta_info)) {
            songs.push_back(std::get<domain::Song>(result.meta_info));
        }
    }
    return songs;
}

domain::AlbumList SearchCollectionWorkflow::collectAlbums(std::string keyword, int limit, int max_page_concurrency)
{
    domain::AlbumList list;
    const int normalized_limit = normalizeWorkflowLimit(limit);
    const int last_page = (std::max)(1, (normalized_limit + kWorkflowSearchRows - 1) / kWorkflowSearchRows);

    for (int first_page = 1; first_page <= last_page && !reachedWorkflowLimit(list.albums.size(), normalized_limit); first_page += workflowPageWindow(max_page_concurrency)) {
        const int current_last_page = (std::min)(last_page, first_page + workflowPageWindow(max_page_concurrency) - 1);
        auto pages = collectOrderedPageWindow<AlbumPageResult>(
            page_runner_,
            first_page,
            current_last_page,
            [this, keyword](int page) {
                return AlbumPageResult {page, collectAlbumPage(keyword, page)};
            },
            "collect album page"
        );

        for (auto &page : pages) {
            for (auto &album : page.data) {
                list.albums.push_back(std::move(album));
                if (reachedWorkflowLimit(list.albums.size(), normalized_limit)) {
                    break;
                }
            }
        }
    }

    if (album_detail_provider_) {
        for (std::size_t first = 0; first < list.albums.size(); first += static_cast<std::size_t>(workflowPageWindow(max_page_concurrency))) {
            const std::size_t last = (std::min)(list.albums.size(), first + static_cast<std::size_t>(workflowPageWindow(max_page_concurrency)));
            std::vector<std::future<AlbumSongsResult>> futures;
            futures.reserve(last - first);

            for (std::size_t i = first; i < last; ++i) {
                domain::Album album = list.albums[i];
                futures.push_back(page_runner_.submit<AlbumSongsResult>(
                    [this, i, album]() {
                        return AlbumSongsResult {static_cast<int>(i), i, collectAlbumSongs(album)};
                    },
                    AlbumSongsResult {static_cast<int>(i), i, {}},
                    "collect album songs"
                ));
            }

            for (auto &future : futures) {
                auto result = future.get();
                if (result.index < list.albums.size()) {
                    list.albums[result.index].vaild_songs = std::move(result.data);
                }
            }
        }
    }

    list.total = static_cast<int>(list.albums.size());
    return list;
}

} // namespace orchestrator::modules::crawler::application::use_cases
