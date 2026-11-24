#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace TestHarness
{
// The actors the harness froze (EngineCalls::FreezeActor). A frozen actor gets no graph update (kProcessMe, 41466), so
// one that dies never finishes dying: it is thawed at its death (docs/re/systems/actor-ai-and-copies.md). Pure: ClientTests.
class FrozenActors
{
public:
    void Set(uint32_t aFormId, bool aFrozen)
    {
        if (aFrozen)
            m_frozen.insert(aFormId);
        else
            m_frozen.erase(aFormId);
    }
    [[nodiscard]] bool Frozen(uint32_t aFormId) const noexcept { return m_frozen.contains(aFormId); }

    // The frozen ones dead now (dying counts), forgotten: the caller thaws them.
    template <class TDead> [[nodiscard]] std::vector<uint32_t> TakeDead(const TDead& acDead)
    {
        std::vector<uint32_t> dead;
        for (auto it = m_frozen.begin(); it != m_frozen.end();)
            if (acDead(*it))
            {
                dead.push_back(*it);
                it = m_frozen.erase(it);
            }
            else
                ++it;
        return dead;
    }

private:
    std::set<uint32_t> m_frozen;
};
} // namespace TestHarness
