#pragma once

#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

class FileStore {
public:
    virtual ~FileStore() = default;
    virtual bool save(std::string_view relative_path, const std::vector<char> &data) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
