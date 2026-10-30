#pragma once
#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/application/model/ResourceTransferQuery.h"
#include <boost/mysql/row_view.hpp>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::resource_queries {
// 单条与批量查询复用相同字段布局与证据规则。
application::model::ResourceCloudReceiptView mapReceipt(boost::mysql::row_view row);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::resource_queries
