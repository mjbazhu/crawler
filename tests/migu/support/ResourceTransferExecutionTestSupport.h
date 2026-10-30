#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"

#include <memory>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
class MySqlConnectionPool;
}
namespace orchestrator::modules::crawler::tests::transfer_execution {

// 独立编译重型 MySQL/云端夹具，验证实际策略与三路状态，不调用真实平台。
void verify(const std::shared_ptr<infrastructure::music::persistence::mysql::MySqlConnectionPool> &pool, application::model::ResourceIdentity identity, std::vector<application::model::ResourceTransferTargetProfile> profiles);

} // namespace orchestrator::modules::crawler::tests::transfer_execution
