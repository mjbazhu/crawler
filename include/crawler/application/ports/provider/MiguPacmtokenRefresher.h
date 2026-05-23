#pragma once

#include "crawler/application/model/MiguPacmtoken.h"

#include <string>

namespace orchestrator::modules::crawler::application::ports {

// Migu pacmtoken 刷新 port，只负责调用平台校验/刷新接口。
class MiguPacmtokenRefresher {
public:
    virtual ~MiguPacmtokenRefresher() = default;

    // 使用当前 pacmtoken 调用 checkPacMtoken，并返回脱敏后的结果状态。
    virtual application::model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string pacmtoken) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
