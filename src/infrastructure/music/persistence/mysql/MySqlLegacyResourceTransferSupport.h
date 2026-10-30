#pragma once

#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/application/model/LegacyResourceTransfer.h"

#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer {
namespace model = application::model;
using Json = nlohmann::json;

// 选取时冻结旧展示身份；传输只使用完整资源身份。
struct Item {
    model::ResourceIdentity identity;
    model::SongResourceProcessItem song;
    model::MetadataCoverItem cover;
};

Json songJson(const model::SongResourceProcessItem &item);
model::SongResourceProcessItem songFromJson(const Json &value);
Json coverJson(const model::MetadataCoverItem &item);
model::MetadataCoverItem coverFromJson(const Json &value);
Json itemsJson(const std::vector<Item> &items);
std::vector<Item> itemsFromJson(const Json &value, model::ResourceOwner owner);
Json batchJson(const model::LegacyResourceTransferBatch &batch);
model::LegacyResourceTransferBatch batchFromJson(const Json &value);

// 选择器仅读取旧语义范围；后续登记在同一外事务，不调用旧claim writer。
boost::asio::awaitable<std::vector<Item>> select(boost::mysql::pooled_connection &connection, const model::LegacyResourceTransferRequest &request, bool facts_only = false, bool page_query = false, std::uint64_t *total = nullptr);
boost::asio::awaitable<std::optional<model::LegacyResourceTransferBatch>> project(boost::mysql::pooled_connection &connection, const std::string &id, bool cancel = false);
boost::asio::awaitable<bool> createLegacyRows(boost::mysql::pooled_connection &connection, const model::LegacyResourceTransferRequest &request, const std::string &id, const std::vector<Item> &items);
boost::asio::awaitable<void> storeItem(boost::mysql::pooled_connection &connection, const std::string &id, std::size_t index, const Item &item, const std::string &state, bool written);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer
