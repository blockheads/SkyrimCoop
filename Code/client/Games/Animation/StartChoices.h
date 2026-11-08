#pragma once

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
// A copy takes the owner's choice nearest its playback time within this: its point is stamped up to a send interval
// after the choice, and the copy's clip clock trails its playback by a few frames.
constexpr uint64_t cPastMs = 250;
constexpr uint64_t cAheadMs = 300;
constexpr size_t cMost = 32; // choices held per actor, oldest dropped

struct Choice
{
    std::string Machine;
    int32_t State = 0;
    uint64_t At = 0; // a copy's: its owner's tick (the point's); an owner's: unused
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
        m_choices.push_back(std::move(aChoice));
        if (m_choices.size() > cMost)
            m_choices.pop_front();
    }

    std::vector<Choice> Drain()
    {
        std::scoped_lock _{m_lock};
        std::vector<Choice> choices(m_choices.begin(), m_choices.end());
        m_choices.clear();
        return choices;
    }

    // The choice for this machine nearest the copy's playback time, taken; passed ones go, later ones wait.
    std::optional<int32_t> Take(std::string_view aMachine, uint64_t aPlayback)
    {
        std::scoped_lock _{m_lock};
        std::erase_if(m_choices, [aPlayback](const Choice& acChoice) { return acChoice.At + cPastMs < aPlayback; });
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
        m_choices.erase(best);
        return cState;
    }

    void Clear()
    {
        std::scoped_lock _{m_lock};
        m_choices.clear();
    }

private:
    std::mutex m_lock;
    std::deque<Choice> m_choices;
};

// At an activation (ChooseStartStateId's hook): an owner records the engine's choice, a copy takes its owner's.
inline int32_t Choose(Queue& aQueue, bool aCopy, std::string_view aMachine, int32_t aEngineChoice, uint64_t aPlayback)
{
    if (!aCopy)
    {
        aQueue.Push({std::string(aMachine), aEngineChoice, 0});
        return aEngineChoice;
    }
    return aQueue.Take(aMachine, aPlayback).value_or(aEngineChoice);
}
} // namespace StartChoices
