#pragma once

#include "crawler/application/model/MiguPacmtoken.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"

#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// Migu pacmtoken 运行期刷新用例，负责校验输入、调用刷新 port 并更新内存 token。
class MiguPacmtokenService {
public:
    // 构造函数：注入平台刷新 port 和运行期 token 存储。
    MiguPacmtokenService(
        std::shared_ptr<application::ports::MiguPacmtokenRefresher> refresher,
        std::shared_ptr<application::ports::MiguPacmtokenStore> store
    );

    // 使用请求中的 token 或当前存储 token 调用 Migu checkPacMtoken。
    application::model::MiguPacmtokenRefreshResult refresh(application::model::MiguPacmtokenRefreshRequest request);

    // 返回当前运行期 pacmtoken 脱敏状态；不包含 token 明文。
    application::model::MiguPacmtokenState state() const;

    // 返回当前运行期 pacmtoken；只供前端显式同步本地 token 使用。
    application::model::MiguPacmtokenState current() const;

private:
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> refresher_; // 平台刷新 port
    std::shared_ptr<application::ports::MiguPacmtokenStore> store_;         // 运行期 token 存储
};

} // namespace orchestrator::modules::crawler::application::use_cases
