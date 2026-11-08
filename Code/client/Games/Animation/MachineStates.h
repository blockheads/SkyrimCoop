#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// A copy's state machines converge on its owner's: the owner sends each state its tracked machines enter, with its time;
// the copy, at the same owner time, moves a machine that differs (docs/re/systems/animation-replay-recovery.md,
// "A copy's machines reconcile to its owner's").
namespace MachineStates
{
constexpr uint64_t cGraceMs = 250; // a copy's machine apart this long, in graph time, is moved (its replay had its turn)
constexpr int32_t cInactive = -1;  // the owner's machine deactivated (Deactivate 59337)
constexpr size_t cMost = 64;       // entries held per actor, oldest dropped

// A machine on both games: its behaviour file and name (FNV-1a). Clone addresses differ between games, these do not.
inline uint32_t Key(std::string_view aBehaviour, std::string_view aMachine) noexcept
{
    uint32_t hash = 2166136261u;
    for (const std::string_view cPart : {aBehaviour, std::string_view("/"), aMachine})
        for (const char c : cPart)
            hash = (hash ^ uint8_t(c)) * 16777619u;
    return hash;
}

// Whether a machine's state is one each game can pick on its own: a variable-bound start, a random draw or a chooser
// (ChooseStartStateId 59405 reads them; startStateMode 0 is the bound or fixed start).
inline bool Tracked(uint8_t aStartMode, bool aBoundStart) noexcept
{
    return aStartMode != 0 || aBoundStart;
}

// The owner's time a copy's graph is at: its playback less how late it took its last replayed action (a late action, a
// lost packet resent, delays the whole graph after it), and never past an action it has yet to take.
inline uint64_t ReplayLag(uint64_t aPlayback, uint64_t aActionTick) noexcept
{
    return aPlayback > aActionTick ? aPlayback - aActionTick : 0;
}

inline uint64_t GraphTime(uint64_t aPlayback, uint64_t aLag, std::optional<uint64_t> aPendingTick = std::nullopt) noexcept
{
    const uint64_t cTime = aPlayback > aLag ? aPlayback - aLag : 0;
    return aPendingTick ? std::min(cTime, *aPendingTick) : cTime;
}

// The owner's frame tick, for its graph job to stamp entries (set each frame by the main thread).
inline std::atomic<uint64_t> g_now{0};

// An actor's machine clones by key, walked from its graph's clone maps; 0 marks a clone with no key of its own (a name
// used twice in one graph). A new root graph (rebuilt) starts it over. Its graph job's alone.
struct KeyCache
{
    const void* Root = nullptr;
    std::unordered_map<const void*, uint32_t> Keys;
};

struct Entry
{
    uint32_t Key = 0;
    int32_t State = 0; // or cInactive
    uint64_t At = 0;   // the owner's tick when its machine entered the state
};

// The wire: generation, then per entry key, state and ms before the point's tick (clamped).
inline std::string Pack(uint16_t aGeneration, const std::vector<Entry>& acEntries, uint64_t aPointTick)
{
    std::string packed(2 + acEntries.size() * 8, '\0');
    std::memcpy(packed.data(), &aGeneration, 2);
    for (size_t i = 0; i < acEntries.size(); ++i)
    {
        const auto& cEntry = acEntries[i];
        const auto cState = int16_t(cEntry.State);
        const auto cBefore = uint16_t(std::min<uint64_t>(aPointTick > cEntry.At ? aPointTick - cEntry.At : 0, 0xFFFF));
        char* pOut = packed.data() + 2 + i * 8;
        std::memcpy(pOut, &cEntry.Key, 4);
        std::memcpy(pOut + 4, &cState, 2);
        std::memcpy(pOut + 6, &cBefore, 2);
    }
    return packed;
}

inline bool Unpack(std::string_view aPacked, uint64_t aPointTick, uint16_t& aGeneration, std::vector<Entry>& aEntries)
{
    if (aPacked.size() < 2 || (aPacked.size() - 2) % 8)
        return false;
    std::memcpy(&aGeneration, aPacked.data(), 2);
    for (size_t offset = 2; offset < aPacked.size(); offset += 8)
    {
        Entry entry;
        int16_t state = 0;
        uint16_t before = 0;
        std::memcpy(&entry.Key, aPacked.data() + offset, 4);
        std::memcpy(&state, aPacked.data() + offset + 4, 2);
        std::memcpy(&before, aPacked.data() + offset + 6, 2);
        entry.State = state;
        entry.At = aPointTick > before ? aPointTick - before : 0;
        aEntries.push_back(entry);
    }
    return true;
}

// An owner's entries to send: the graph job pushes, the main thread drains.
struct Outbox
{
    void Push(Entry aEntry)
    {
        std::scoped_lock _{m_lock};
        m_entries.push_back(aEntry);
        if (m_entries.size() > cMost)
            m_entries.pop_front();
    }

    std::vector<Entry> Drain()
    {
        std::scoped_lock _{m_lock};
        std::vector<Entry> entries(m_entries.begin(), m_entries.end());
        m_entries.clear();
        return entries;
    }

private:
    std::mutex m_lock;
    std::deque<Entry> m_entries;
};

// A copy's view of its owner's machines. The main thread adds entries and sets the graph time together; the graph job
// asks, per machine, where it should be.
struct Timelines
{
    // The owner's entries from one point; another generation's (its graph rebuilt) replace everything.
    void Add(uint16_t aGeneration, const std::vector<Entry>& acEntries)
    {
        std::scoped_lock _{m_lock};
        if (aGeneration != m_generation)
        {
            m_entries.clear();
            m_machines.clear();
            m_generation = aGeneration;
        }
        for (const auto& cEntry : acEntries)
        {
            m_entries.push_back(cEntry);
            if (m_entries.size() > cMost)
                m_entries.pop_front();
        }
        Prune();
        m_any.store(!m_entries.empty(), std::memory_order_release);
    }

    void SetGraphTime(uint64_t aGraphTime)
    {
        std::scoped_lock _{m_lock};
        m_graphTime = aGraphTime;
        Prune();
    }

    // The copy's graph rebuilt: nothing it knew holds.
    void Clear()
    {
        std::scoped_lock _{m_lock};
        m_entries.clear();
        m_machines.clear();
        m_any.store(false, std::memory_order_release);
    }

    // Read every update of every machine, so lock-free.
    [[nodiscard]] bool Any() const noexcept
    {
        return m_any.load(std::memory_order_acquire);
    }

    // The owner's state for this machine at the copy's graph time, while the owner's is active.
    std::optional<Entry> Wanted(uint32_t aKey)
    {
        std::scoped_lock _{m_lock};
        return WantedLocked(aKey);
    }

    // Called in the copy's machine update: the state to go to, once it has been apart for cGraceMs of graph time.
    std::optional<int32_t> Reconcile(uint32_t aKey, int32_t aCurrent)
    {
        std::scoped_lock _{m_lock};
        const auto cWanted = WantedLocked(aKey);
        auto& machine = m_machines[aKey];
        if (!cWanted || cWanted->State == aCurrent)
        {
            machine.ApartSince.reset();
            return std::nullopt;
        }
        const uint64_t cSince = std::max(cWanted->At, machine.ApartSince.value_or(m_graphTime));
        if (!machine.ApartSince)
            machine.ApartSince = m_graphTime;
        if (m_graphTime < cSince + cGraceMs)
            return std::nullopt;
        machine.ApartSince = m_graphTime; // another try only after another grace
        return cWanted->State;
    }

private:
    struct Machine
    {
        std::optional<uint64_t> ApartSince;
    };

    std::optional<Entry> WantedLocked(uint32_t aKey) const
    {
        const Entry* pBest = nullptr;
        for (const auto& cEntry : m_entries)
            if (cEntry.Key == aKey && cEntry.At <= m_graphTime && (!pBest || cEntry.At >= pBest->At))
                pBest = &cEntry;
        if (!pBest || pBest->State == cInactive)
            return std::nullopt;
        return *pBest;
    }

    // Per machine, entries before its latest one at the graph time are history.
    void Prune()
    {
        std::map<uint32_t, uint64_t> latest;
        for (const auto& cEntry : m_entries)
            if (cEntry.At <= m_graphTime)
                latest[cEntry.Key] = std::max(latest[cEntry.Key], cEntry.At);
        std::erase_if(m_entries, [&](const Entry& acEntry) {
            const auto cIt = latest.find(acEntry.Key);
            return cIt != latest.end() && acEntry.At < cIt->second;
        });
    }

    std::mutex m_lock;
    std::deque<Entry> m_entries;
    std::map<uint32_t, Machine> m_machines;
    uint64_t m_graphTime = 0;
    uint16_t m_generation = 0;
    std::atomic<bool> m_any{false};
};
} // namespace MachineStates
