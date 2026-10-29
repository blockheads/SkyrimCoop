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

bool SystemQuestList::Learn(uint32_t aQuest) noexcept
{
    std::scoped_lock _{m_lock};
    m_any = true;
    return m_quests.try_emplace(aQuest).second;
}

bool SystemQuestList::Contains(uint32_t aQuest) noexcept
{
    if (!m_any.load(std::memory_order_relaxed))
        return false;
    std::scoped_lock _{m_lock};
    return m_quests.contains(aQuest);
}

std::vector<uint32_t> SystemQuestList::All() noexcept
{
    std::scoped_lock _{m_lock};
    std::vector<uint32_t> quests;
    for (const auto& [cQuest, cGlobals] : m_quests)
        quests.push_back(cQuest);
    return quests;
}

void SystemQuestList::SetGlobals(uint32_t aQuest, const std::vector<uint32_t>& acGlobals) noexcept
{
    std::scoped_lock _{m_lock};
    const auto it = m_quests.find(aQuest);
    if (it == m_quests.end())
        return;
    it->second = acGlobals;
    m_globals.insert(acGlobals.begin(), acGlobals.end());
}

std::vector<uint32_t> SystemQuestList::WithoutGlobals() noexcept
{
    if (!m_any.load(std::memory_order_relaxed))
        return {};
    std::scoped_lock _{m_lock};
    std::vector<uint32_t> quests;
    for (const auto& [cQuest, cGlobals] : m_quests)
        if (!cGlobals)
            quests.push_back(cQuest);
    return quests;
}

bool SystemQuestList::IsGlobal(uint32_t aGlobal) noexcept
{
    if (!m_any.load(std::memory_order_relaxed))
        return false;
    std::scoped_lock _{m_lock};
    return m_globals.contains(aGlobal);
}

std::vector<uint32_t> SystemQuestList::Globals() noexcept
{
    std::scoped_lock _{m_lock};
    return {m_globals.begin(), m_globals.end()};
}

void SystemQuestList::Clear() noexcept
{
    std::scoped_lock _{m_lock};
    m_quests.clear();
    m_globals.clear();
    m_any = false;
}
} // namespace OwnEvents
