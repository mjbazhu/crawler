#pragma once

#include "crawler/application/ports/provider/IMusicProvider.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu 音乐平台 provider，只负责调用 HTTP client 获取平台原始响应。
class MiguProvider : public application::ports::IMusicProvider {
public:
    // 构造函数：依赖注入 HTTP 客户端。
    explicit MiguProvider(std::shared_ptr<http::client::HttpClient> client);

    // 构造函数：依赖注入 HTTP 客户端和运行期 pacmtoken 存储。
    MiguProvider(
        std::shared_ptr<http::client::HttpClient> client,
        std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
    );
    ~MiguProvider() override = default;

    // 获取歌曲搜索原始响应。
    std::optional<Response> fetchSongSearchPage(std::string keyword, int page, int rows) override;

    // 获取歌曲资源接口原始响应；参数来自歌曲来源和音质参数表。
    std::optional<Response> fetchSongResource(application::model::SongResourceFetchRequest request) override;

    // 获取专辑搜索原始响应。
    std::optional<Response> fetchAlbumSearchPage(std::string keyword, int page, int rows) override;

    // 获取专辑下歌曲原始响应。
    std::optional<Response> fetchAlbumSongs(const domain::Album &album) override;

    // 获取歌手专辑单页原始响应。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t singer_id, int page) override;

    // 获取歌手主要信息原始响应。
    std::optional<Response> fetchSingerInfo(std::uint64_t singer_id) override;

    // 获取专辑详情原始响应；Migu resourceinfo.do 按 source_res_type 映射 resourceType。
    std::optional<Response> fetchAlbumDetail(std::string album_id, std::string source_res_type) override;

    // 获取歌手歌曲单页原始响应；Migu 当前 type=1 表示歌曲列表。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t singer_id, int page) override;

    // 获取歌手分类列表原始响应，仅允许 Migu 支持的 tab 组合。
    std::optional<Response> fetchSingerIndexPage(std::string tab) override;

    // 通过 URL 获取原始响应，负责 Migu 页面 URL 到资源 API 的归一化。
    std::optional<Response> fetchUrl(std::string url) override;

    // 返回 Migu 支持的歌手分类 tab 组合。
    static const std::vector<std::string> &singerIndexTabs();

    // 兼容调试入口，等价于 fetchSongSearchPage。
    std::optional<Response> fetchSongSearchPageSync(std::string keyword, int page, int rows);

    // 兼容调试入口，等价于 fetchSongResource。
    std::optional<Response> fetchSongResourceSync(application::model::SongResourceFetchRequest request);

    // 兼容调试入口，等价于 fetchAlbumSearchPage。
    std::optional<Response> fetchAlbumSearchPageSync(std::string keyword, int page, int rows);

    // 兼容调试入口，等价于 fetchAlbumSongs。
    std::optional<Response> fetchAlbumSongsSync(const domain::Album &album);

    // 兼容调试入口，等价于 fetchSingerAlbumsPage。
    std::optional<Response> fetchSingerAlbumsPageSync(std::uint64_t singer_id, int page);

    // 兼容调试入口，等价于 fetchSingerInfo。
    std::optional<Response> fetchSingerInfoSync(std::uint64_t singer_id);

    // 兼容调试入口，等价于 fetchAlbumDetail。
    std::optional<Response> fetchAlbumDetailSync(std::string album_id, std::string source_res_type = {});

    // 兼容调试入口，等价于 fetchSingerSongsPage。
    std::optional<Response> fetchSingerSongsPageSync(std::uint64_t singer_id, int page);

    // 兼容调试入口，等价于 fetchSingerIndexPage。
    std::optional<Response> fetchSingerIndexPageSync(std::string tab);

    // 兼容调试入口，等价于 fetchUrl。
    std::optional<Response> fetchUrlSync(std::string url);

private:
    std::shared_ptr<http::client::HttpClient> client_;                        // 出站 HTTP client
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store_; // 运行期 pacmtoken 存储
    std::vector<std::string> headers_;                                        // Migu 默认请求头
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
