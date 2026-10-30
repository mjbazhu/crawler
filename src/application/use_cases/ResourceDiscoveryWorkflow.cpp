#include "crawler/application/use_cases/ResourceDiscoveryWorkflow.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {
using RunResult = result::ServiceResult<model::ResourceDiscoveryRun>;
// 精确范围不能因目标切换或读到另一个库而继续。
bool scope(const model::LegacyResourceImportPage &page, const model::ResourceDiscoveryRun &lease)
{
    const auto &d = lease.definition;
    const auto &target = page.targets.at(static_cast<std::size_t>(d.destination));
    return page.database_identity == d.database_identity && page.high_water_id == d.high_water_id && page.after_id == lease.cursor && target.revision == d.target_revision && target.fingerprint == d.target_fingerprint;
}
// 只记录本次完成元数据读取的时间，不提升为内容核验时刻。
std::uint64_t now()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
} // namespace
ResourceDiscoveryWorkflow::ResourceDiscoveryWorkflow(ports::ResourceDiscoveryRepository &runs, ports::LegacyResourceImportRepository &sources, ports::ResourceDiscoveryProbe &probe) : runs_(runs), sources_(sources), probe_(probe)
{}
RunResult ResourceDiscoveryWorkflow::step(std::string id)
{
    const auto claimed = runs_.claim(id);
    if (!claimed.isSuccess()) return RunResult::failure(claimed.error(), claimed.message());
    if (!*claimed.data()) return runs_.find(std::move(id));
    const auto lease = **claimed.data();
    const auto &d = lease.definition;
    const auto fail = [&](std::string code) {
        const auto released = runs_.release(lease, code);
        if (!released.isSuccess()) return RunResult::failure(released.error(), released.message());
        return RunResult::failure(result::ServiceErrorType::Conflict, std::move(code));
    };
    try {
        model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::milliseconds(model::resourceDiscoveryPageBudget(d)), {}};
        auto identity_context = context;
        identity_context.deadline = std::min(context.deadline, std::chrono::steady_clock::now() + std::chrono::seconds(10));
        const auto before = probe_.fingerprint(identity_context);
        if (!before.isSuccess() || *before.data() != d.target_fingerprint) return fail("discovery_target_unavailable");
        const auto page = sources_.preview(d.owner, lease.cursor, d.high_water_id, static_cast<std::size_t>(d.page_size));
        if (!page.isSuccess()) return fail("discovery_source_unavailable");
        if (!scope(*page.data(), lease)) return fail("discovery_scope_changed");
        std::vector<model::ResourceDiscoveryObservation> values;
        for (std::size_t start = 0; start < page.data()->rows.size(); start += static_cast<std::size_t>(d.concurrency)) {
            const auto valid = runs_.current(lease);
            if (!valid.isSuccess()) return fail("discovery_lease_unavailable");
            if (!*valid.data()) return runs_.find(id);
            if (context.stopped()) return fail("discovery_page_timeout");
            std::vector<std::future<model::ResourceDiscoveryObservation>> workers;
            const auto end = std::min(page.data()->rows.size(), start + static_cast<std::size_t>(d.concurrency));
            for (auto i = start; i < end; ++i) {
                workers.push_back(std::async(std::launch::async, [&, source = page.data()->rows[i]] {
                    const auto path = model::resourceDiscoveryCandidate(source, d);
                    model::ResourceObjectObservation object;
                    if (!path.empty()) {
                        auto bounded = context;
                        bounded.deadline = std::min(context.deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(d.object_timeout_ms));
                        const auto observed = probe_.observe(path, bounded);
                        if (observed.isSuccess()) object = *observed.data();
                    }
                    auto value = model::classifyResourceDiscovery(source, path, object);
                    value.observed_at = now();
                    return value;
                }));
            }
            for (auto &worker : workers) values.push_back(worker.get());
        }
        if (context.stopped()) return fail("discovery_page_timeout");
        identity_context.deadline = std::min(context.deadline, std::chrono::steady_clock::now() + std::chrono::seconds(10));
        const auto after = probe_.fingerprint(identity_context);
        if (!after.isSuccess() || *after.data() != d.target_fingerprint) return fail("discovery_target_changed");
        const auto refreshed = sources_.preview(d.owner, lease.cursor, d.high_water_id, static_cast<std::size_t>(d.page_size));
        if (!refreshed.isSuccess() || !scope(*refreshed.data(), lease)) return fail("discovery_scope_changed");
        for (auto &value : values) {
            const auto found = std::find_if(refreshed.data()->rows.begin(), refreshed.data()->rows.end(), [&](const auto &s) { return s.identity == value.resource; });
            if (found == refreshed.data()->rows.end() || found->generation != value.generation || found->source_identity != value.source_identity || found->values_digest != value.values_digest) {
                value.grade = "source_changed";
                value.result_code = "source_changed";
            }
        }
        const auto committed = runs_.commit(lease, *page.data(), std::move(values));
        // 提交响应未知时不释放/覆盖，调用者须先重读持久检查点。
        if (!committed.isSuccess()) return RunResult::failure(committed.error(), committed.message());
        return runs_.find(std::move(id));
    } catch (...) {
        return fail("discovery_page_failed");
    }
}
} // namespace orchestrator::modules::crawler::application::use_cases
