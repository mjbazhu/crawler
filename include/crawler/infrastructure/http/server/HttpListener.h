#pragma once

#include <string>
#include <functional>

namespace orchestrator::modules::crawler::infrastructure::http::server {

class HttpListener {
public:
    using RequestCallback = std::function<void(int client_fd)>; // int is client socket

    virtual ~HttpListener() = default;

    virtual void start(const std::string &address, int port, RequestCallback callback) = 0;
    virtual void stop() = 0;

protected:
    virtual void handleConnection(int client_fd) = 0;
};

} // namespace orchestrator::modules::crawler::infrastructure::http::server
