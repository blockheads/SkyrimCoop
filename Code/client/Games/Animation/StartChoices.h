#pragma once

#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A copy's state machines start where their owner's did. A machine's start state can come from a graph variable
// bound to its startStateId (hkbVariableBindingSet 59467, read by ChooseStartStateId 59405), a random draw or a
// chooser: each game reads its own at the moment the machine activates, often a clip's end with no action. The owner
// sends each such choice; its copy takes it at the same activation. docs/re/systems/animation-replay-recovery.md.
namespace StartChoices
{
constexpr uint64_t cKeepMs = 1500; // a copy takes an owner's choice this long after it arrived
constexpr size_t cMost = 32;       // choices held per actor, oldest dropped

struct Choice
{
    std::string Machine;
    int32_t State = 0;
    uint64_t At = 0; // when made (owner) or received (copy), steady milliseconds
};

// Whether the owner's activation is one its copy could make otherwise: not entered through a nested-state transition
// (an override its copy's event sets too), and started by a variable, a random draw or a chooser (mode 1 to 3).
inline bool Shared(int32_t aOverride, uint8_t aStartMode, bool aBoundStart) noexcept
{
    return aOverride < 0 && (aStartMode != 0 || aBoundStart);
}

inline uint64_t Now() noexcept
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
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

    // The oldest kept choice for this machine, taken; expired ones go.
    std::optional<int32_t> Take(std::string_view aMachine, uint64_t aNow)
    {
        std::scoped_lock _{m_lock};
        while (!m_choices.empty() && m_choices.front().At + cKeepMs < aNow)
            m_choices.pop_front();
        for (auto it = m_choices.begin(); it != m_choices.end(); ++it)
            if (it->Machine == aMachine)
            {
                const int32_t cState = it->State;
                m_choices.erase(it);
                return cState;
            }
        return std::nullopt;
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
inline int32_t Choose(Queue& aQueue, bool aCopy, std::string_view aMachine, int32_t aEngineChoice, uint64_t aNow)
{
    if (!aCopy)
    {
        aQueue.Push({std::string(aMachine), aEngineChoice, aNow});
        return aEngineChoice;
    }
    return aQueue.Take(aMachine, aNow).value_or(aEngineChoice);
}
} // namespace StartChoices
