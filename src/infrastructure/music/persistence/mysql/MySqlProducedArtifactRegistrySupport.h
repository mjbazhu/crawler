#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/model/ResourceTransferJobs.h"
#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <cstdint>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::produced_artifacts {
// 调用方已锁定并核实自身任务；本函数只校验资源版本及绑定内容参考，不提交事务。
boost::asio::awaitable<std::vector<application::model::ResourceIdentity>> recordOnConnection(boost::mysql::pooled_connection &connection, const application::model::ResourceTransferJob &parent, const application::model::LyricArtifactBundle &bundle, std::uint64_t source_size);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::produced_artifacts
