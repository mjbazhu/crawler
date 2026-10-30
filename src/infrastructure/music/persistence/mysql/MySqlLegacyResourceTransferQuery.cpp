#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransfer.h"
#include "MySqlLegacyResourceTransferSupport.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
application::result::ServiceResult<application::model::MetadataCoverPage> MySqlLegacyResourceTransfer::listCovers(application::model::LegacyResourceTransferRequest request)
{
    auto work = [request = std::move(request)](auto &connection) -> boost::asio::awaitable<application::model::MetadataCoverPage> {
        const auto profile = co_await transfer_jobs::query(connection, "resource_legacy_profiles/lock");
        const auto provider = request.cloud_provider ? std::string(application::model::resourceDestinationName(*request.cloud_provider)) : "local";
        if (!profile.rows().empty() && MySqlValueReader::stringAt(profile.rows().front(), 0) != provider) throw std::logic_error("legacy provider is immutable");
        application::model::MetadataCoverPage page;
        page.page = request.covers.query.page;
        page.page_size = request.covers.query.page_size;
        for (auto &item : co_await legacy_transfer::select(connection, request, true, true, &page.total)) page.items.push_back(std::move(item.cover.record));
        co_return page;
    };
    return transfer_jobs::run(pool_, transfer_jobs::transaction<application::model::MetadataCoverPage>(pool_, std::move(work)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
