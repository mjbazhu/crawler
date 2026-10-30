#pragma once

#include "crawler/application/model/MiguPacmtoken.h"

#include <string>
#include <utility>

namespace orchestrator::modules::crawler::application::ports {

// Migu pacmtoken 刷新 port，只负责调用平台校验/刷新接口。
class MiguPacmtokenRefresher {
public:
    virtual ~MiguPacmtokenRefresher() = default;

    // 使用当前 pacmtoken 调用 checkPacMtoken，并返回脱敏后的结果状态。
    virtual application::model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string pacmtoken) = 0;
    // 来源执行器可协调周期/强制刷新；旧实现保持原有逐调用语义。
    virtual application::model::MiguPacmtokenRefreshResult refreshPacmtokenWithPolicy(std::string pacmtoken, bool, int)
    {
        return refreshPacmtoken(std::move(pacmtoken));
    }
};

} // namespace orchestrator::modules::crawler::application::ports
