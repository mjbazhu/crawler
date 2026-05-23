#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

std::optional<application::model::AlbumSourceMetadata> parseAlbumSourceFromListItem(const nlohmann::json &item)
{
    if (!hasValidAlbumItem(item)) {
        return std::nullopt;
    }

    const std::string source_action = JsonUtils::getString(item, "action");
    const std::string source_album_id = firstNonEmpty({
        JsonUtils::getString(item, "resId"),
        extractActionId(source_action),
    });
    const std::string source_title = JsonUtils::getString(item, "txt");
    if (source_album_id.empty() || source_title.empty()) {
        return std::nullopt;
    }

    application::model::AlbumSourceMetadata metadata;
    metadata.source = "migu";
    metadata.title = source_title;
    metadata.normalized_title = source_title;
    metadata.artist_name = JsonUtils::getString(item, "txt2");
    metadata.source_item_key = "album:" + source_album_id;
    metadata.source_album_id = source_album_id;
    metadata.source_res_type = JsonUtils::getString(item, "resType");
    metadata.source_action = source_action;
    metadata.source_album_kind = albumKindFromTypes(metadata.source_res_type, {});
    metadata.source_title = source_title;
    metadata.source_artist_name = metadata.artist_name;
    metadata.cover_url = JsonUtils::getString(item, "img");
    metadata.release_date = JsonUtils::getString(item, "txt3");
    metadata.detail_sync_status = 0;
    return metadata;
}

std::optional<application::model::AlbumSourceMetadata> parseAlbumSourceFromDetailObject(const nlohmann::json &item)
{
    const std::string source_album_id = firstNonEmpty({
        JsonUtils::getString(item, "contentId"),
        JsonUtils::getString(item, "albumId"),
    });
    const std::string source_title = JsonUtils::getString(item, "title");
    if (source_album_id.empty() || source_title.empty()) {
        return std::nullopt;
    }

    application::model::AlbumSourceMetadata metadata;
    metadata.source = "migu";
    metadata.title = source_title;
    metadata.normalized_title = source_title;
    metadata.artist_name = JsonUtils::getString(item, "singer");
    metadata.source_item_key = "album:" + source_album_id;
    metadata.source_album_id = source_album_id;
    metadata.source_material_id = JsonUtils::getString(item, "materialId");
    metadata.source_resource_type = JsonUtils::getString(item, "resourceType");
    metadata.source_album_kind = albumKindFromTypes({}, metadata.source_resource_type);
    metadata.source_title = source_title;
    metadata.source_artist_id = JsonUtils::getString(item, "singerId");
    metadata.source_artist_name = metadata.artist_name;
    metadata.cover_url = firstNonEmpty({
        selectImageUrl(JsonUtils::getArray(item, "imgItem")),
        selectImageUrl(JsonUtils::getArray(item, "imgItems")),
    });
    metadata.summary = JsonUtils::getString(item, "summary");
    metadata.price = JsonUtils::getString(item, "price");
    metadata.total_count = JsonUtils::getString(item, "totalCount");
    metadata.release_date = firstNonEmpty({
        JsonUtils::getString(item, "firstStartDate"),
        JsonUtils::getString(item, "issueDate"),
        JsonUtils::getString(item, "publishDate"),
        JsonUtils::getString(item, "publishTime"),
    });
    metadata.publish_date = firstNonEmpty({
        JsonUtils::getString(item, "publishDate"),
        JsonUtils::getString(item, "publishTime"),
    });
    metadata.issue_date = JsonUtils::getString(item, "issueDate");
    metadata.online_date = JsonUtils::getString(item, "onlineDate");
    metadata.copyright_id = JsonUtils::getString(item, "copyrightId");
    metadata.source_item_id = JsonUtils::getString(item, "itemId");
    metadata.source_campaign_id = JsonUtils::getString(item, "redHareId");
    metadata.presale_date = JsonUtils::getString(item, "presaleDate");
    metadata.first_start_date = JsonUtils::getString(item, "firstStartDate");
    metadata.first_end_date = JsonUtils::getString(item, "firstEndDate");
    metadata.sale_end_date = JsonUtils::getString(item, "saleEndDate");
    metadata.invalidate_date = JsonUtils::getString(item, "invalidateDate");
    metadata.original_first_start_date = JsonUtils::getString(item, "originalFirstStartDate");
    metadata.original_first_end_date = JsonUtils::getString(item, "originalfirstEndDate");
    metadata.digital_period_status = JsonUtils::getString(item, "dalbumPeriodStatus");
    metadata.vip_type = JsonUtils::getString(item, "vipType");
    metadata.vip_flag = JsonUtils::getString(item, "vipFlag");
    metadata.vip_start_time = JsonUtils::getString(item, "vipStartTime");
    metadata.vip_end_time = JsonUtils::getString(item, "vipEndTime");
    metadata.effective_status = JsonUtils::getString(item, "effectiveStatus");
    metadata.convert_status = JsonUtils::getString(item, "isConvert");
    metadata.forever_listen_flag = JsonUtils::getString(item, "foreverListenFlag");
    metadata.forever_listen = parseTriStateFlag(item, "foreverListen");
    metadata.is_before_sale_end = parseTriStateFlag(item, "isBeforeSaleEndDate");
    metadata.is_in_first_date = parseTriStateFlag(item, "isInFirstdate");
    metadata.is_call_payment = parseTriStateFlag(item, "isCallPayment");
    metadata.is_call_payment_for_present = parseTriStateFlag(item, "isCallPaymentForPresent");
    metadata.is_call_payment_for_purchase = parseTriStateFlag(item, "isCallPaymentForPurchase");
    metadata.is_knowledge_paid = parseTriStateFlag(item, "isKnowledgePaid");
    metadata.have_filter_tag = parseTriStateFlag(item, "haveFilterTag");
    metadata.is_convert = parseTriStateFlag(item, "isConvert");
    metadata.is_appointment = parseTriStateFlag(item, "isAppointment");
    if (const auto *op_num = JsonUtils::getObject(item, "opNumItem")) {
        metadata.play_num = static_cast<std::uint64_t>(JsonUtils::getInt(*op_num, "playNum"));
        metadata.play_num_desc = JsonUtils::getString(*op_num, "playNumDesc");
    }
    metadata.detail_sync_status = 2;
    return metadata;
}

void collectAlbumSources(const nlohmann::json &node, std::vector<application::model::AlbumSourceMetadata> &albums)
{
    if (node.is_array()) {
        for (const auto &item : node) {
            collectAlbumSources(item, albums);
        }
        return;
    }

    if (!node.is_object()) {
        return;
    }

    if (auto metadata = parseAlbumSourceFromListItem(node)) {
        albums.push_back(std::move(*metadata));
        return;
    }
    if (auto metadata = parseAlbumSourceFromDetailObject(node)) {
        albums.push_back(std::move(*metadata));
        return;
    }

    if (const auto *data = JsonUtils::getObject(node, "data")) {
        collectAlbumSources(*data, albums);
    }
    if (const auto *resource = JsonUtils::getArray(node, "resource")) {
        collectAlbumSources(*resource, albums);
    }
    if (const auto *contents = JsonUtils::getArray(node, "contents")) {
        collectAlbumSources(*contents, albums);
    }
}

std::vector<application::model::AlbumSourceMetadata> parseAlbumSourcesRaw(const std::vector<char> &raw_data)
{
    std::vector<application::model::AlbumSourceMetadata> albums;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return albums;
    }

    collectAlbumSources(*root_opt, albums);
    return albums;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
