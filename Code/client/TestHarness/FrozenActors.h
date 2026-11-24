#pragma once

#include <cstdint>
#include <set>

namespace TestHarness
{
// The actors the harness froze (EngineCalls::FreezeActor). A frozen actor gets no graph update (kProcessMe, 41466), so
// one that dies never finishes dying: it is thawed at its death (docs/re/systems/actor-ai-and-copies.md). Pure: ClientTests.
class FrozenActors
{
public:
    enum class Life
    {
        kAlive,
        kDead, // dying counts
        kGone, // no such actor any more (deleted, unloaded with its game)
    };

    void Set(uint32_t aFormId, bool aFrozen)
    {
        if (aFrozen)
            m_frozen.insert(aFormId);
        else
            m_frozen.erase(aFormId);
    }
    [[nodiscard]] bool Frozen(uint32_t aFormId) const noexcept { return m_frozen.contains(aFormId); }

    // Each frame: a dead one is thawed, a gone one forgotten (its saved freeze too), both dropped.
    template <class TLife, class TThaw, class TForget> void Poll(const TLife& acLife, const TThaw& acThaw, const TForget& acForget)
    {
        for (auto it = m_frozen.begin(); it != m_frozen.end();)
        {
            const uint32_t cFormId = *it;
            const Life cLife = acLife(cFormId);
            if (cLife == Life::kAlive)
            {
                ++it;
                continue;
            }
            it = m_frozen.erase(it);
            if (cLife == Life::kDead)
                acThaw(cFormId);
            else
                acForget(cFormId);
        }
    }

private:
    std::set<uint32_t> m_frozen;
};
} // namespace TestHarness
