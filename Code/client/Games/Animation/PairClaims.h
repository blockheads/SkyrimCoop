#pragma once

#include <Games/Animation/ReplayState.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

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

    // Kept by action tick: a copy's replay behind its owner still finds an earlier answer.
    void Set(uint32_t aActorId, const Answer& acAnswer) noexcept
    {
        auto& answers = m_answers[aActorId];
        std::erase_if(answers, [&](const Answer& acKept) { return acKept.Tick == acAnswer.Tick; });
        if (answers.size() == cKept)
            answers.erase(answers.begin());
        answers.push_back(acAnswer);
    }
    // The pair ended on its claimer's game: a replay of it arriving later is not played (refused).
    void Done(uint32_t aActorId) noexcept
    {
        if (const auto cAnswers = m_answers.find(aActorId); cAnswers != m_answers.end())
            for (Answer& answer : cAnswers->second)
                answer.Claimed = false;
    }
    void Clear() noexcept { m_answers.clear(); }

    // The claim of the kill move aActorId swung at aTick.
    [[nodiscard]] ReplayState::PairClaim Of(uint32_t aActorId, uint64_t aTick) const noexcept
    {
        const auto cAnswers = m_answers.find(aActorId);
        if (cAnswers == m_answers.end())
            return ReplayState::PairClaim::kUnknown;
        const auto cAnswer = std::ranges::find(cAnswers->second, aTick, &Answer::Tick);
        if (cAnswer == cAnswers->second.end())
            return ReplayState::PairClaim::kUnknown;
        return cAnswer->Claimed ? ReplayState::PairClaim::kClaimed : ReplayState::PairClaim::kRefused;
    }

    // aActorId holds a claimed pair with aPartnerId (a forwarded hit between them is that pair's blow).
    [[nodiscard]] bool Holds(uint32_t aActorId, uint32_t aPartnerId) const noexcept
    {
        const auto cAnswers = m_answers.find(aActorId);
        return cAnswers != m_answers.end() &&
               std::ranges::any_of(cAnswers->second, [&](const Answer& acAnswer) { return acAnswer.Claimed && acAnswer.PartnerId == aPartnerId; });
    }

private:
    static constexpr size_t cKept = 8; // answers per actor, the oldest dropped
    std::unordered_map<uint32_t, std::vector<Answer>> m_answers;
};
