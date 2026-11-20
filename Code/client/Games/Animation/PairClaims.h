#pragma once

#include <Games/Animation/ReplayState.h>

#include <cstdint>
#include <unordered_map>

// The host's answers to pair claims as this game heard them (PairService), by the claiming actor's session id:
// what a copy's replayed kill move goes by (AnimationSystem). docs/design/one-simulator.md, piece 4b.
struct PairClaims
{
    struct Answer
    {
        uint32_t PartnerId{}; // session id
        uint64_t Tick{};      // the claimed action's
        bool Claimed{};       // false: refused
    };

    void Set(uint32_t aActorId, const Answer& acAnswer) noexcept { m_answers[aActorId] = acAnswer; }
    // The pair ended on its claimer's game: a replay of it arriving later is not played (refused).
    void Done(uint32_t aActorId) noexcept
    {
        if (const auto cAnswer = m_answers.find(aActorId); cAnswer != m_answers.end())
            cAnswer->second.Claimed = false;
    }
    void Clear() noexcept { m_answers.clear(); }

    // The claim of the kill move aActorId swung at aTick.
    [[nodiscard]] ReplayState::PairClaim Of(uint32_t aActorId, uint64_t aTick) const noexcept
    {
        const auto cAnswer = m_answers.find(aActorId);
        if (cAnswer == m_answers.end() || cAnswer->second.Tick != aTick)
            return ReplayState::PairClaim::kUnknown;
        return cAnswer->second.Claimed ? ReplayState::PairClaim::kClaimed : ReplayState::PairClaim::kRefused;
    }

    // aActorId holds a claimed pair with aPartnerId (a forwarded hit between them is that pair's blow).
    [[nodiscard]] bool Holds(uint32_t aActorId, uint32_t aPartnerId) const noexcept
    {
        const auto cAnswer = m_answers.find(aActorId);
        return cAnswer != m_answers.end() && cAnswer->second.Claimed && cAnswer->second.PartnerId == aPartnerId;
    }

private:
    std::unordered_map<uint32_t, Answer> m_answers;
};
