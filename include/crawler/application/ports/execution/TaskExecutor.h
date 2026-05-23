#pragma once

#include <functional>

namespace orchestrator::modules::crawler::application::ports {

class TaskExecutor {
public:
    virtual ~TaskExecutor() = default;
    virtual void submit(std::function<void()> task) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
