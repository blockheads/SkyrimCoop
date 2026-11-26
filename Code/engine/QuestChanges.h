#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

// Coalesced quest writes and the alias references whose placement or sharing can change their snapshots.
class QuestChanges
{
  public:
    void Quest(uint32_t aId)
    {
        std::scoped_lock lock(m_lock);
        m_changed.insert(aId);
    }

    void Reference(uint32_t aId)
    {
        std::scoped_lock lock(m_lock);
        if (const auto it = m_dependents.find(aId); it != m_dependents.end())
            m_changed.insert(it->second.begin(), it->second.end());
    }

    void Track(uint32_t aQuest, const std::set<uint32_t>& acReferences)
    {
        std::scoped_lock lock(m_lock);
        for (const uint32_t cRef : m_references[aQuest])
        {
            auto it = m_dependents.find(cRef);
            it->second.erase(aQuest);
            if (it->second.empty())
                m_dependents.erase(it);
        }
        m_references[aQuest] = acReferences;
        for (const uint32_t cRef : acReferences)
            m_dependents[cRef].insert(aQuest);
    }

    std::set<uint32_t> Take()
    {
        std::scoped_lock lock(m_lock);
        return std::exchange(m_changed, {});
    }

    void Clear()
    {
        std::scoped_lock lock(m_lock);
        m_changed.clear();
        m_references.clear();
        m_dependents.clear();
    }

  private:
    std::mutex m_lock;
    std::set<uint32_t> m_changed;
    std::map<uint32_t, std::set<uint32_t>> m_references;
    std::map<uint32_t, std::set<uint32_t>> m_dependents;
};
