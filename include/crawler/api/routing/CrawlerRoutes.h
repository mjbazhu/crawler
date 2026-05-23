#pragma once

#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/routing/RouteSink.h"

#include <memory>

namespace orchestrator::modules::crawler::api::routing {

class CrawlerRoutes {
public:
    explicit CrawlerRoutes(std::shared_ptr<api::controllers::CrawlerController> controller);

    void registerRoutes(RouteSink &routes) const;

private:
    std::shared_ptr<api::controllers::CrawlerController> controller_;
};

} // namespace orchestrator::modules::crawler::api::routing
