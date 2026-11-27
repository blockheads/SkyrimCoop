#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Each actor's clip sets (an action's, an instant interaction's, a movement idle's) by its graph manager's address:
// the event loaded into each, which a sync point names, and its loads since its last op, for its stream. An entry
// goes when its manager does (63355), as a new manager can take the freed address.
class ActorClipSets
{
public:
    using Loads = std::vector<std::pair<uint8_t, std::string>>;

    void Record(const void* apManager, uint8_t aSet, const char* acpEvent)
    {
        std::scoped_lock _{m_lock};
        auto& entry = m_byManager[apManager];
        entry.Held[aSet] = acpEvent;
        if (entry.Unsent.size() < 16)
            entry.Unsent.emplace_back(aSet, acpEvent);
    }

    // Its loads since the last call (or sync point).
    Loads TakeUnsent(const void* apManager)
    {
        std::scoped_lock _{m_lock};
        const auto it = m_byManager.find(apManager);
        return it == m_byManager.end() ? Loads{} : std::exchange(it->second.Unsent, {});
    }

    // What its sync point names: its held sets, and its unsent loads with them.
    std::map<uint8_t, std::string> TakeHeld(const void* apManager)
    {
        std::scoped_lock _{m_lock};
        const auto it = m_byManager.find(apManager);
        if (it == m_byManager.end())
            return {};
        it->second.Unsent.clear();
        return it->second.Held;
    }

    void Forget(const void* apManager)
    {
        std::scoped_lock _{m_lock};
        m_byManager.erase(apManager);
    }

private:
    struct Entry
    {
        std::map<uint8_t, std::string> Held;
        Loads Unsent;
    };
    std::mutex m_lock;
    std::unordered_map<const void*, Entry> m_byManager;
};
