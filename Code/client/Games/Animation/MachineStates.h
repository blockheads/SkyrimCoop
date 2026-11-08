#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A copy's state machines converge on its owner's: the owner sends each state its machines enter, with its time, and a
// baseline of them all when asked; the copy, at the same owner time, moves a machine that differs
// (docs/re/systems/animation-replay-recovery.md, "A copy's machines reconcile to its owner's").
namespace MachineStates
{
constexpr uint64_t cGraceMs = 250;    // a copy's machine apart this long, in graph time, is moved (its replay had its turn)
constexpr uint64_t cHistoryMs = 2000; // owner states kept behind the graph time: it can step back (a late action)
constexpr int32_t cInactive = INT32_MIN; // the owner's machine deactivated (Deactivate 59337); no state id is this
constexpr size_t cMostPending = 64;   // an owner's unsent changes; past it, a baseline goes instead
constexpr size_t cMostPerMachine = 8; // a copy's entries per machine
constexpr size_t cMostMachines = 512; // machines per actor, owner's current states or copy's timelines
constexpr size_t cEntryBytes = 12;    // generation u16, key u32, state i32, ms before the batch u16
constexpr size_t cMostBatchEntries = cMostMachines + cMostPending;

// A machine on both games: its name and its states' names (FNV-1a). It reads the clone itself, so a clone freed and
// another made at its address cannot carry the old one's key; two machines alike in all of these are one to us.
struct KeyHash
{
    uint32_t Value = 2166136261u;
    void Add(std::string_view aText) noexcept
    {
        for (const char c : aText)
            Value = (Value ^ uint8_t(c)) * 16777619u;
    }
};

// aStates: its states' names joined by ','.
inline uint32_t Key(std::string_view aMachine, std::string_view aStates) noexcept
{
    KeyHash hash;
    hash.Add(aMachine);
    hash.Add("|");
    hash.Add(aStates);
    return hash.Value;
}

// The owner's time a copy's graph is at: its playback less how late it took its last replayed action (a late action, a
// lost packet resent, delays what follows it), the lateness spent as playback goes on so the graph catches up, and
// never past an action it has yet to take.
inline uint64_t ReplayLag(uint64_t aPlayback, uint64_t aActionTick) noexcept
{
    return aPlayback > aActionTick ? aPlayback - aActionTick : 0;
}

inline uint64_t GraphTime(uint64_t aPlayback, uint64_t aLag, uint64_t aTakenAt, std::optional<uint64_t> aPendingTick = std::nullopt) noexcept
{
    const uint64_t cSpent = aPlayback > aTakenAt ? aPlayback - aTakenAt : 0;
    const uint64_t cLag = aLag > cSpent ? aLag - cSpent : 0;
    const uint64_t cTime = aPlayback > cLag ? aPlayback - cLag : 0;
    return aPendingTick ? std::min(cTime, *aPendingTick) : cTime;
}

// The owner's frame tick, for its graph job to stamp entries (set each frame by the main thread).
inline std::atomic<uint64_t> g_now{0};

struct Entry
{
    uint32_t Key = 0;
    int32_t State = 0;       // or cInactive
    uint64_t At = 0;         // the owner's tick when its machine entered the state
    uint16_t Generation = 0; // the owner's graph's (a rebuilt graph counts up)
};

inline std::string Pack(const std::vector<Entry>& acEntries, uint64_t aBatchTick)
{
    std::string packed(acEntries.size() * cEntryBytes, '\0');
    for (size_t i = 0; i < acEntries.size(); ++i)
    {
        const auto& cEntry = acEntries[i];
        const auto cBefore = uint16_t(std::min<uint64_t>(aBatchTick > cEntry.At ? aBatchTick - cEntry.At : 0, 0xFFFF));
        char* pOut = packed.data() + i * cEntryBytes;
        std::memcpy(pOut, &cEntry.Generation, 2);
        std::memcpy(pOut + 2, &cEntry.Key, 4);
        std::memcpy(pOut + 6, &cEntry.State, 4);
        std::memcpy(pOut + 10, &cBefore, 2);
    }
    return packed;
}

// Refuses a batch that is not whole entries or holds more than an owner can send.
inline bool Unpack(std::string_view aPacked, uint64_t aBatchTick, std::vector<Entry>& aEntries)
{
    if (aPacked.size() % cEntryBytes || aPacked.size() / cEntryBytes > cMostBatchEntries)
        return false;
    for (size_t offset = 0; offset < aPacked.size(); offset += cEntryBytes)
    {
        Entry entry;
        uint16_t before = 0;
        std::memcpy(&entry.Generation, aPacked.data() + offset, 2);
        std::memcpy(&entry.Key, aPacked.data() + offset + 2, 4);
        std::memcpy(&entry.State, aPacked.data() + offset + 6, 4);
        std::memcpy(&before, aPacked.data() + offset + 10, 2);
        entry.At = aBatchTick > before ? aBatchTick - before : 0;
        aEntries.push_back(entry);
    }
    return true;
}

// An owner's machines: the graph job pushes each change, the main thread drains. It keeps every machine's current state,
// the baseline sent when a copy asks, when its graph is rebuilt, or when unsent changes overflowed.
struct Outbox
{
    void Push(Entry aEntry)
    {
        std::scoped_lock _{m_lock};
        if (m_current.size() < cMostMachines || m_current.contains(aEntry.Key))
            m_current[aEntry.Key] = aEntry;
        m_pending.push_back(aEntry);
        if (m_pending.size() > cMostPending)
        {
            m_pending.clear();
            m_baseline = true;
        }
    }

    // Its graph rebuilt: the old machines are gone, the next drain is the new graph's states alone.
    void Rebuilt()
    {
        std::scoped_lock _{m_lock};
        m_current.clear();
        m_pending.clear();
        m_baseline = true;
    }

    void RequestBaseline()
    {
        std::scoped_lock _{m_lock};
        m_baseline = true;
    }

    std::vector<Entry> Drain()
    {
        std::scoped_lock _{m_lock};
        std::vector<Entry> entries;
        if (std::exchange(m_baseline, false))
            for (const auto& [cKey, cEntry] : m_current)
                entries.push_back(cEntry);
        else
            entries.assign(m_pending.begin(), m_pending.end());
        m_pending.clear();
        return entries;
    }

private:
    std::mutex m_lock;
    std::map<uint32_t, Entry> m_current;
    std::deque<Entry> m_pending;
    bool m_baseline = false;
};

// What a copy's machine is doing, read in its update.
struct Machine
{
    int32_t Current = 0;
    std::optional<int32_t> Heading; // a running transition's destination (activeTransitions)
};

// A copy's view of its owner's machines. The main thread adds entries and sets the graph time; the graph job asks, per
// machine, where it should be.
struct Timelines
{
    void Add(const std::vector<Entry>& acEntries)
    {
        std::scoped_lock _{m_lock};
        for (const auto& cEntry : acEntries)
        {
            if (!m_entries.contains(cEntry.Key) && m_entries.size() >= cMostMachines)
                continue;
            auto& entries = m_entries[cEntry.Key];
            entries.insert(std::ranges::upper_bound(entries, cEntry.At, {}, &Entry::At), cEntry);
            if (entries.size() > cMostPerMachine)
                entries.pop_front();
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

    [[nodiscard]] uint64_t GraphTime()
    {
        std::scoped_lock _{m_lock};
        return m_graphTime;
    }

    // The copy's graph rebuilt: nothing it knew holds (it asks its owner for a baseline).
    void Clear()
    {
        std::scoped_lock _{m_lock};
        m_entries.clear();
        m_apart.clear();
        m_generation.reset();
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

    // Called in the copy's machine update: the state to go to, once it has been apart for cGraceMs of graph time. A
    // machine heading to the wanted state is not apart; one heading elsewhere is, whatever its current state says.
    std::optional<int32_t> Reconcile(uint32_t aKey, const Machine& acMachine)
    {
        std::scoped_lock _{m_lock};
        const auto cWanted = WantedLocked(aKey);
        const int32_t cWhere = acMachine.Heading.value_or(acMachine.Current);
        if (!cWanted || cWanted->State == cWhere)
        {
            m_apart.erase(aKey);
            return std::nullopt;
        }
        const auto cApart = m_apart.find(aKey);
        if (cApart == m_apart.end())
        {
            if (m_apart.size() < cMostMachines)
                m_apart[aKey] = m_graphTime;
            return std::nullopt;
        }
        if (m_graphTime < std::max(cWanted->At, cApart->second) + cGraceMs)
            return std::nullopt;
        cApart->second = m_graphTime; // another try only after another grace
        return cWanted->State;
    }

private:
    // The latest entry at or before the graph time, of the owner's generation then.
    std::optional<Entry> WantedLocked(uint32_t aKey) const
    {
        const auto cIt = m_entries.find(aKey);
        if (cIt == m_entries.end())
            return std::nullopt;
        const Entry* pBest = nullptr;
        for (const auto& cEntry : cIt->second)
            if (cEntry.At <= m_graphTime)
                pBest = &cEntry;
        if (!pBest || pBest->State == cInactive || (m_generation && pBest->Generation != *m_generation))
            return std::nullopt;
        return *pBest;
    }

    // The owner's generation at the graph time is its latest entry's then. History more than cHistoryMs behind the graph
    // time goes, keeping each machine's latest entry at or before it.
    void Prune()
    {
        const Entry* pLatest = nullptr;
        for (const auto& [cKey, cEntries] : m_entries)
            for (const auto& cEntry : cEntries)
                if (cEntry.At <= m_graphTime && (!pLatest || cEntry.At > pLatest->At))
                    pLatest = &cEntry;
        if (pLatest)
            m_generation = pLatest->Generation;
        for (auto& [cKey, entries] : m_entries)
            while (entries.size() > 1 && entries[1].At + cHistoryMs < m_graphTime)
                entries.pop_front();
    }

    std::mutex m_lock;
    std::map<uint32_t, std::deque<Entry>> m_entries;
    std::map<uint32_t, uint64_t> m_apart;
    std::optional<uint16_t> m_generation;
    uint64_t m_graphTime = 0;
    std::atomic<bool> m_any{false};
};
} // namespace MachineStates
