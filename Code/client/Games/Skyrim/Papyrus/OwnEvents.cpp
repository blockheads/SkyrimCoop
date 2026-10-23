#include <Papyrus/OwnEvents.h>

namespace OwnEvents
{
bool OwnQuests::Started(uint32_t aQuest, bool aWasRunning, bool aStarted, bool aIsGuest, uint32_t aCause) noexcept
{
    if (!aStarted || aWasRunning || !aIsGuest || aCause != kThisPlayer)
        return false;
    std::scoped_lock _{m_lock};
    m_quests.insert(aQuest);
    m_any = true;
    return true;
}

bool OwnQuests::Contains(uint32_t aQuest) noexcept
{
    if (!m_any.load(std::memory_order_relaxed))
        return false;
    std::scoped_lock _{m_lock};
    return m_quests.contains(aQuest);
}

void OwnQuests::Clear() noexcept
{
    std::scoped_lock _{m_lock};
    m_quests.clear();
    m_any = false;
}
} // namespace OwnEvents
