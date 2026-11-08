#pragma once

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A copy's state machines start where their owner's did (ChooseStartStateId 59405): the owner sends its choices, its
// copy takes them by the owner's time. docs/re/systems/animation-replay-recovery.md, "A machine's start state".
namespace StartChoices
{
// An owner's choice and a copy's start match when the choice's time (its packet's) is within this of the copy's graph
// time at the start (animation-replay-recovery.md).
constexpr uint64_t cPastMs = 250;
constexpr uint64_t cAheadMs = 300;
constexpr uint64_t cLateMs = 1000; // how long a copy's missed start waits for its choice, in playback time
constexpr size_t cMost = 32; // choices, misses or corrections held per actor, oldest dropped

struct Choice
{
    std::string Machine;
    int32_t State = 0;
    uint64_t At = 0; // a copy's: its owner's tick (the point's); an owner's: unused
};

// The owner's time a copy's graph is at: its playback less how late it took its last replayed action (a late action, a
// lost packet resent, delays the whole graph after it, its starts too), and never past an action it has yet to take.
inline uint64_t ReplayLag(uint64_t aPlayback, uint64_t aActionTick) noexcept
{
    return aPlayback > aActionTick ? aPlayback - aActionTick : 0;
}

inline uint64_t GraphTime(uint64_t aPlayback, uint64_t aLag, std::optional<uint64_t> aPendingTick = std::nullopt) noexcept
{
    const uint64_t cTime = aPlayback > aLag ? aPlayback - aLag : 0;
    return aPendingTick ? std::min(cTime, *aPendingTick) : cTime;
}

// A copy's start no choice had reached yet: the state it entered, at that playback time.
struct Miss
{
    std::string Machine;
    int32_t Entered = 0;
    uint64_t At = 0;
};

// A choice that came after its start: the copy's machine goes there if still in the state it entered.
struct Correction
{
    std::string Machine;
    int32_t From = 0;
    int32_t To = 0;
    uint64_t At = 0; // the copy's playback time when the choice came
};

// Whether the owner's activation is one its copy could make otherwise: not entered through a nested-state transition
// (an override its copy's event sets too), and started by a variable, a random draw or a chooser (mode 1 to 3).
inline bool Shared(int32_t aOverride, uint8_t aStartMode, bool aBoundStart) noexcept
{
    return aOverride < 0 && (aStartMode != 0 || aBoundStart);
}

inline std::string Pack(const std::vector<Choice>& acChoices)
{
    std::string packed;
    for (const auto& cChoice : acChoices)
        packed += cChoice.Machine + ':' + std::to_string(cChoice.State) + ';';
    return packed;
}

inline std::vector<Choice> Unpack(std::string_view aPacked, uint64_t aAt)
{
    std::vector<Choice> choices;
    while (!aPacked.empty())
    {
        const auto cEnd = aPacked.find(';');
        const auto cEntry = aPacked.substr(0, cEnd);
        const auto cColon = cEntry.rfind(':');
        if (cEnd == std::string_view::npos || cColon == std::string_view::npos)
            break;
        Choice choice{std::string(cEntry.substr(0, cColon)), 0, aAt};
        std::from_chars(cEntry.data() + cColon + 1, cEntry.data() + cEntry.size(), choice.State);
        choices.push_back(std::move(choice));
        aPacked.remove_prefix(cEnd + 1);
    }
    return choices;
}

// An actor's choices: an owner's to send, a copy's received. The graph job writes or takes them, the main thread the other.
struct Queue
{
    void Push(Choice aChoice)
    {
        std::scoped_lock _{m_lock};
        Bounded(m_choices, std::move(aChoice));
    }

    // A copy's received choice: one for a start already missed becomes a correction, else it waits for its start.
    void Receive(Choice aChoice, uint64_t aPlayback)
    {
        std::scoped_lock _{m_lock};
        Expire(aPlayback);
        auto miss = m_misses.end();
        uint64_t bestGap = 0;
        for (auto it = m_misses.begin(); it != m_misses.end(); ++it)
        {
            if (it->Machine != aChoice.Machine || aChoice.At + cPastMs < it->At || aChoice.At > it->At + cAheadMs)
                continue;
            const uint64_t cGap = aChoice.At > it->At ? aChoice.At - it->At : it->At - aChoice.At;
            if (miss == m_misses.end() || cGap < bestGap)
                miss = it, bestGap = cGap;
        }
        if (miss == m_misses.end())
            return Bounded(m_choices, std::move(aChoice));
        if (miss->Entered != aChoice.State)
            Bounded(m_corrections, Correction{std::move(aChoice.Machine), miss->Entered, aChoice.State, aPlayback});
        m_misses.erase(miss);
        m_hasCorrections.store(!m_corrections.empty(), std::memory_order_release);
    }

    // A copy's start that found no choice: a later one for it corrects the machine.
    void Missed(std::string_view aMachine, int32_t aEntered, uint64_t aPlayback)
    {
        std::scoped_lock _{m_lock};
        Bounded(m_misses, Miss{std::string(aMachine), aEntered, aPlayback});
    }

    // Read every graph update of every machine, so lock-free: whether Correct could have anything.
    [[nodiscard]] bool HasCorrections() const noexcept
    {
        return m_hasCorrections.load(std::memory_order_acquire);
    }

    // The state this machine goes to, taken: only while it is still in the state it entered.
    std::optional<int32_t> Correct(std::string_view aMachine, int32_t aCurrent, uint64_t aPlayback)
    {
        std::scoped_lock _{m_lock};
        Expire(aPlayback);
        const auto cIt = std::ranges::find(m_corrections, aMachine, &Correction::Machine);
        std::optional<int32_t> state;
        if (cIt != m_corrections.end())
        {
            if (cIt->From == aCurrent)
                state = cIt->To;
            m_corrections.erase(cIt);
        }
        m_hasCorrections.store(!m_corrections.empty(), std::memory_order_release);
        return state;
    }

    std::vector<Choice> Drain()
    {
        std::scoped_lock _{m_lock};
        std::vector<Choice> choices(m_choices.begin(), m_choices.end());
        m_choices.clear();
        return choices;
    }

    // The choice for this machine nearest the copy's playback time, taken; passed ones go, later ones wait.
    // apGap: the taken choice's time less the playback; apExpired: passed choices dropped, never taken.
    std::optional<int32_t> Take(std::string_view aMachine, uint64_t aPlayback, int64_t* apGap = nullptr, size_t* apExpired = nullptr)
    {
        std::scoped_lock _{m_lock};
        const size_t cExpired = std::erase_if(m_choices, [aPlayback](const Choice& acChoice) { return acChoice.At + cPastMs < aPlayback; });
        if (apExpired)
            *apExpired = cExpired;
        auto best = m_choices.end();
        uint64_t bestGap = 0;
        for (auto it = m_choices.begin(); it != m_choices.end(); ++it)
        {
            if (it->Machine != aMachine || it->At > aPlayback + cAheadMs)
                continue;
            const uint64_t cGap = it->At > aPlayback ? it->At - aPlayback : aPlayback - it->At;
            if (best == m_choices.end() || cGap < bestGap)
                best = it, bestGap = cGap;
        }
        if (best == m_choices.end())
            return std::nullopt;
        const int32_t cState = best->State;
        if (apGap)
            *apGap = static_cast<int64_t>(best->At) - static_cast<int64_t>(aPlayback);
        m_choices.erase(best);
        return cState;
    }

    void Clear()
    {
        std::scoped_lock _{m_lock};
        m_choices.clear();
        m_misses.clear();
        m_corrections.clear();
        m_hasCorrections.store(false, std::memory_order_release);
    }

private:
    template <class T> static void Bounded(std::deque<T>& aItems, T aItem)
    {
        aItems.push_back(std::move(aItem));
        if (aItems.size() > cMost)
            aItems.pop_front();
    }

    // Misses no choice can still match, and corrections a machine never took (it left the state, or stopped).
    void Expire(uint64_t aPlayback)
    {
        std::erase_if(m_misses, [aPlayback](const Miss& acMiss) { return acMiss.At + cLateMs < aPlayback; });
        std::erase_if(m_corrections, [aPlayback](const Correction& acCorrection) { return acCorrection.At + cLateMs < aPlayback; });
    }

    std::mutex m_lock;
    std::deque<Choice> m_choices;
    std::deque<Miss> m_misses;
    std::deque<Correction> m_corrections;
    std::atomic<bool> m_hasCorrections{false};
};

// At an activation (ChooseStartStateId's hook): an owner records the engine's choice, a copy takes its owner's or
// notes the miss (a later choice then corrects it, Receive and Correct).
inline int32_t Choose(Queue& aQueue, bool aCopy, std::string_view aMachine, int32_t aEngineChoice, uint64_t aPlayback,
                      int64_t* apGap = nullptr, size_t* apExpired = nullptr)
{
    if (!aCopy)
    {
        aQueue.Push({std::string(aMachine), aEngineChoice, 0});
        return aEngineChoice;
    }
    if (const auto cTaken = aQueue.Take(aMachine, aPlayback, apGap, apExpired))
        return *cTaken;
    aQueue.Missed(aMachine, aEngineChoice, aPlayback);
    return aEngineChoice;
}
} // namespace StartChoices
