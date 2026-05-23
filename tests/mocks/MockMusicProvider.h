#pragma once

#include "crawler/application/ports/provider/IMusicProvider.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::tests {

class MockMusicProvider : public application::ports::IMusicProvider {
public:
    // 预设返回值
    std::optional<Response> mock_song_search_response;
    std::optional<Response> mock_album_search_response;
    std::optional<Response> mock_album_songs_response;
    std::optional<Response> mock_singer_albums_response;
    std::optional<Response> mock_album_detail_response;
    std::optional<Response> mock_singer_songs_response;
    std::optional<Response> mock_singer_index_response;
    std::optional<Response> mock_song_resource_response;
    std::optional<Response> mock_url_response;

    // 记录调用
    std::string last_fetch_url;
    std::string last_search_query;
    int last_page = 0;
    int last_rows = 0;
    std::uint64_t last_singer_id = 0;
    std::string last_singer_index_tab;
    application::model::SongResourceFetchRequest last_song_resource_request;
    std::string last_album_detail_id;
    std::string last_album_detail_source_res_type;

    std::optional<Response> fetchSongSearchPage(std::string keyword, int page, int rows) override
    {
        last_search_query = std::move(keyword);
        last_page = page;
        last_rows = rows;
        return mock_song_search_response;
    }

    std::optional<Response> fetchSongResource(application::model::SongResourceFetchRequest request) override
    {
        last_song_resource_request = std::move(request);
        return mock_song_resource_response;
    }

    std::optional<Response> fetchAlbumSearchPage(std::string keyword, int page, int rows) override
    {
        last_search_query = std::move(keyword);
        last_page = page;
        last_rows = rows;
        return mock_album_search_response;
    }

    std::optional<Response> fetchAlbumSongs(const domain::Album &album) override
    {
        (void)album;
        return mock_album_songs_response;
    }

    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t singer_id, int page) override
    {
        last_singer_id = singer_id;
        last_page = page;
        return mock_singer_albums_response;
    }

    std::optional<Response> fetchAlbumDetail(std::string album_id, std::string source_res_type) override
    {
        last_album_detail_id = std::move(album_id);
        last_album_detail_source_res_type = std::move(source_res_type);
        return mock_album_detail_response;
    }

    std::optional<Response> fetchSingerSongsPage(std::uint64_t singer_id, int page) override
    {
        last_singer_id = singer_id;
        last_page = page;
        return mock_singer_songs_response;
    }

    std::optional<Response> fetchSingerIndexPage(std::string tab) override
    {
        last_singer_index_tab = std::move(tab);
        return mock_singer_index_response;
    }

    std::optional<Response> fetchUrl(std::string url) override
    {
        last_fetch_url = std::move(url);
        return mock_url_response;
    }
};

} // namespace orchestrator::modules::crawler::tests
