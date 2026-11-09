#include "Scheduled.h"
#include "Commands.h"
#include "Harness.h"

#include <chrono>

namespace TestHarness
{
double Scheduled::NowMs() noexcept
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t Scheduled::Queue(nlohmann::json aRequest, double aAtMs)
{
    aRequest.erase("at");
    const uint32_t cId = m_nextId++;
    m_waiting.push_back({cId, aAtMs, std::move(aRequest)});
    return cId;
}

void Scheduled::Poll(Harness& aHarness)
{
    ++Frame;
    if (m_waiting.empty())
        return;
    const double cNow = NowMs();
    std::vector<Entry> due;
    std::erase_if(m_waiting, [&](Entry& aEntry) {
        if (aEntry.AtMs > cNow)
            return false;
        due.push_back(std::move(aEntry));
        return true;
    });
    for (const Entry& cEntry : due)
    {
        auto result = nlohmann::json::parse(HandleCommand(aHarness, cEntry.Request.dump()), nullptr, false);
        m_done[std::to_string(cEntry.Id)] = {{"frame", Frame}, {"ms", cNow}, {"late_ms", cNow - cEntry.AtMs}, {"result", result}};
    }
}

nlohmann::json Scheduled::Take(uint32_t aId)
{
    const auto cKey = std::to_string(aId);
    if (!m_done.contains(cKey))
        return {{"ok", true}, {"done", false}, {"waiting", m_waiting.size()}};
    nlohmann::json taken = std::move(m_done[cKey]);
    m_done.erase(cKey);
    taken["ok"] = true;
    taken["done"] = true;
    return taken;
}
} // namespace TestHarness
